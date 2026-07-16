#include "FFmpegPacketQueue.h"

extern "C"
{
#include <libavcodec/packet.h>
}

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <string_view>

namespace
{
using namespace std::chrono_literals;

void Require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(message.data());
    }
}

AVPacket* PacketOfSize(int size)
{
    AVPacket* packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, size) < 0)
    {
        av_packet_free(&packet);
        throw std::runtime_error("packet allocation failed");
    }
    return packet;
}

void TestFlushOrderingAndStalePush()
{
    FFmpegPacketQueue queue(4);
    queue.BeginGeneration(2, true);

    AVPacket* stale = PacketOfSize(8);
    Require(!queue.Push(stale, 1), "stale generation push was accepted");
    av_packet_free(&stale);

    AVPacket* input = PacketOfSize(17);
    Require(queue.Push(input, 2), "current generation push was rejected");
    av_packet_free(&input);

    AVPacket* output = av_packet_alloc();
    const auto flush = queue.Pop(output);
    Require(flush.kind == FFmpegPacketQueue::PopKind::Flush && flush.generation == 2, "flush was not first");
    const auto packet = queue.Pop(output);
    Require(
        packet.kind == FFmpegPacketQueue::PopKind::Packet && packet.generation == 2 && output->size == 17,
        "packet did not follow flush"
    );
    av_packet_free(&output);
    queue.Stop();
}

void TestCleanupAndBlockedProducerWake()
{
    ReadAheadCache budget;
    budget.Configure(true, 1024);
    FFmpegPacketQueue queue(1);
    queue.SetBudget(&budget);
    queue.BeginGeneration(3, false);

    AVPacket* first = PacketOfSize(64);
    Require(queue.Push(first, 3), "initial queue fill failed");
    av_packet_free(&first);
    Require(budget.UsedBytes() == 64, "queued packet was not budgeted");

    auto producer = std::async(
        std::launch::async,
        [&queue]
        {
            AVPacket* packet = PacketOfSize(32);
            const bool pushed = queue.Push(packet, 3);
            av_packet_free(&packet);
            return pushed;
        }
    );
    Require(producer.wait_for(50ms) == std::future_status::timeout, "producer did not block on full queue");
    queue.Interrupt();
    const bool producerWoke = producer.wait_for(1s) == std::future_status::ready;
    if (!producerWoke)
    {
        queue.Stop();
    }
    Require(producerWoke, "interruption did not wake producer");
    Require(!producer.get(), "interrupted producer reported success");
    Require(budget.UsedBytes() == 0, "interruption did not release queued packet budget");
}

void TestConsumerRestartAndRepeatedInterrupt()
{
    ReadAheadCache budget;
    budget.Configure(true, 1024);
    FFmpegPacketQueue queue;
    queue.SetBudget(&budget);
    queue.BeginGeneration(4, false);

    auto consumer = std::async(
        std::launch::async,
        [&queue]
        {
            AVPacket* packet = av_packet_alloc();
            const auto result = queue.Pop(packet);
            av_packet_free(&packet);
            return result;
        }
    );
    Require(consumer.wait_for(50ms) == std::future_status::timeout, "consumer did not block on empty queue");
    queue.Interrupt();
    queue.Interrupt();
    budget.Reset();
    queue.BeginGeneration(5, true);
    const bool consumerWoke = consumer.wait_for(1s) == std::future_status::ready;
    if (!consumerWoke)
    {
        queue.Stop();
    }
    Require(consumerWoke, "new generation did not wake consumer");
    const auto result = consumer.get();
    Require(
        result.kind == FFmpegPacketQueue::PopKind::Flush && result.generation == 5,
        "consumer did not restart at the new generation flush"
    );
    queue.Stop();
}

void TestEofSeekRestartAndTerminalStop()
{
    FFmpegPacketQueue queue;
    queue.BeginGeneration(6, false);
    queue.SignalEof();

    AVPacket* packet = av_packet_alloc();
    const auto eof = queue.Pop(packet);
    Require(eof.kind == FFmpegPacketQueue::PopKind::Eof && eof.generation == 6, "EOF generation mismatch");
    queue.AcknowledgeEof(6);
    Require(queue.WaitForEof(6), "EOF acknowledgement was not observed");

    queue.Interrupt();
    queue.BeginGeneration(7, true);
    const auto flush = queue.Pop(packet);
    Require(
        flush.kind == FFmpegPacketQueue::PopKind::Flush && flush.generation == 7, "EOF did not restart with a flush"
    );
    queue.Stop();
    const auto stop = queue.Pop(packet);
    Require(stop.kind == FFmpegPacketQueue::PopKind::Stop && stop.generation == 7, "terminal stop was not delivered");
    av_packet_free(&packet);
}
} // namespace

int main()
{
    try
    {
        TestFlushOrderingAndStalePush();
        TestCleanupAndBlockedProducerWake();
        TestConsumerRestartAndRepeatedInterrupt();
        TestEofSeekRestartAndTerminalStop();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FFmpegPacketQueueTests: %s\n", error.what());
        return 1;
    }
}
