#pragma once

// Cross-plugin query surface for AI-generated per-file tags produced by the AITagger
// plugin. Tags are (tag, confidence, model id) rows keyed by file path, stored in the
// shared media store. Other plugins (Playlist, search, …) read them here; they never
// link AITagger. Discover with ctx.GetService<IMediaTags>() and null-check.
//
// This is a query interface only — tags are *produced* by the tagging worker and
// *announced* with MediaTagsUpdatedEvent (see <framelift/Events.h>); consumers refresh
// on that event and re-query here. POD buf/cap idiom (like IHistory); adding it does
// not bump FRAMELIFT_ABI_VERSION.
//
// Threading: backed by the media store's per-thread connections, so these may be called
// from any thread. Count/index iteration is a best-effort snapshot — a concurrent run
// may change the row set between GetTagCount and GetTag; treat a stale index gracefully
// (GetTag returns -1 past the end).
class IMediaTags
{
public:
    static constexpr const char* InterfaceId = "framelift.IMediaTags";
    virtual ~IMediaTags() = default;

    // Number of present (above-threshold) tags recorded for `path`, or 0 if none.
    [[nodiscard]] virtual int GetTagCount(const char* path) const noexcept = 0;

    // The `index`-th present tag for `path` (0-based, ordered by descending confidence).
    // Copies the tag string into buf/cap (buf may be null to query length) and returns
    // its full length excl. NUL, or -1 if index is out of range. `confidence` (may be
    // null) receives the score in [0,1]; modelBuf/modelCap (modelBuf may be null)
    // receive the producing model id.
    [[nodiscard]] virtual int GetTag(
        const char* path, int index, char* buf, int cap, float* confidence, char* modelBuf, int modelCap
    ) const noexcept = 0;

    // True iff `path` has `tag` recorded at confidence >= minConfidence.
    [[nodiscard]] virtual bool HasTag(const char* path, const char* tag, float minConfidence) const noexcept = 0;
};
