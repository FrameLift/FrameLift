#include "ModelDownloader.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace
{
QString UrlFor(const aitagger::CatalogEntry& e, int which)
{
    return QString::fromStdString(which == 0 ? e.modelUrl : e.mmprojUrl);
}

std::string ShaFor(const aitagger::CatalogEntry& e, int which)
{
    return which == 0 ? e.modelSha256 : e.mmprojSha256;
}

qint64 SizeFor(const aitagger::CatalogEntry& e, int which)
{
    return which == 0 ? e.modelSize : e.mmprojSize;
}
} // namespace

ModelDownloader::ModelDownloader(QObject* parent) : QObject(parent), nam_(std::make_unique<QNetworkAccessManager>())
{
}

ModelDownloader::~ModelDownloader()
{
    Cancel();
}

QString ModelDownloader::ModelsDir()
{
    const QString dir = QCoreApplication::applicationDirPath() + "/models";
    QDir().mkpath(dir);
    return dir;
}

QString ModelDownloader::ModelPath(const QString& id)
{
    return ModelsDir() + "/" + id + ".gguf";
}

QString ModelDownloader::MmprojPath(const QString& id)
{
    return ModelsDir() + "/" + id + ".mmproj.gguf";
}

bool ModelDownloader::IsInstalled(const QString& id)
{
    return QFile::exists(ModelPath(id)) && QFile::exists(MmprojPath(id));
}

void ModelDownloader::Start(const aitagger::CatalogEntry& entry)
{
    if (busy_)
    {
        return;
    }
    entry_ = entry;
    busy_ = true;
    StartFile(0);
}

void ModelDownloader::StartFile(int which)
{
    which_ = which;
    const QString id = QString::fromStdString(entry_.id);
    targetPath_ = which == 0 ? ModelPath(id) : MmprojPath(id);

    // Already present ⇒ skip to the next stage (or finish).
    if (QFile::exists(targetPath_))
    {
        if (which_ == 0)
        {
            StartFile(1);
        }
        else
        {
            const QString doneId = id;
            Cleanup();
            Q_EMIT finished(doneId, true, QString());
        }
        return;
    }

    const QString partPath = targetPath_ + ".part";
    file_ = std::make_unique<QFile>(partPath);
    resumeFrom_ = QFile::exists(partPath) ? QFileInfo(partPath).size() : 0;
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Append))
    {
        Fail("cannot open " + partPath);
        return;
    }
    received_ = resumeFrom_;
    total_ = SizeFor(entry_, which_);

    QNetworkRequest req{QUrl(UrlFor(entry_, which_))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resumeFrom_ > 0)
    {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(resumeFrom_) + "-");
    }
    reply_ = nam_->get(req);
    connect(reply_, &QNetworkReply::readyRead, this, &ModelDownloader::OnReadyRead);
    connect(reply_, &QNetworkReply::finished, this, &ModelDownloader::OnFinished);
}

void ModelDownloader::OnReadyRead()
{
    if (!reply_ || !file_)
    {
        return;
    }
    const QByteArray chunk = reply_->readAll();
    file_->write(chunk);
    received_ += chunk.size();

    if (total_ <= 0)
    {
        const QVariant len = reply_->header(QNetworkRequest::ContentLengthHeader);
        if (len.isValid())
        {
            total_ = resumeFrom_ + len.toLongLong();
        }
    }
    // Fraction across both files: model is the first half, mmproj the second.
    const double fileFrac = total_ > 0 ? static_cast<double>(received_) / total_ : 0.0;
    const double overall = (which_ + fileFrac) / 2.0;
    Q_EMIT progress(QString::fromStdString(entry_.id), overall);
}

void ModelDownloader::OnFinished()
{
    if (!reply_)
    {
        return;
    }
    const QNetworkReply::NetworkError err = reply_->error();
    reply_->deleteLater();
    reply_ = nullptr;
    if (file_)
    {
        file_->flush();
        file_->close();
    }

    if (err != QNetworkReply::NoError)
    {
        // Keep the .part for a later resume; report the failure.
        Fail("network error while downloading " + targetPath_);
        return;
    }

    const QString partPath = targetPath_ + ".part";
    const std::string expected = ShaFor(entry_, which_);
    if (!expected.empty() && !VerifySha(partPath, expected))
    {
        QFile::remove(partPath); // corrupt — force a clean re-download
        Fail("checksum mismatch for " + targetPath_);
        return;
    }
    QFile::remove(targetPath_);
    if (!QFile::rename(partPath, targetPath_))
    {
        Fail("cannot finalize " + targetPath_);
        return;
    }

    if (which_ == 0)
    {
        StartFile(1);
    }
    else
    {
        const QString doneId = QString::fromStdString(entry_.id);
        Cleanup();
        Q_EMIT finished(doneId, true, QString());
    }
}

bool ModelDownloader::VerifySha(const QString& path, const std::string& expectedHex) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
    {
        return false;
    }
    return hash.result().toHex().compare(QByteArray::fromStdString(expectedHex), Qt::CaseInsensitive) == 0;
}

void ModelDownloader::Cancel()
{
    if (reply_)
    {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (file_)
    {
        file_->flush();
        file_->close();
        file_.reset();
    }
    busy_ = false;
}

void ModelDownloader::Fail(const QString& error)
{
    const QString id = QString::fromStdString(entry_.id);
    Cleanup();
    Q_EMIT finished(id, false, error);
}

void ModelDownloader::Cleanup()
{
    if (file_)
    {
        file_->close();
        file_.reset();
    }
    busy_ = false;
}
