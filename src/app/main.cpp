#include "cryptsh/ui/MainWindow.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <memory>

namespace {

QString contextAddQueueDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(base).filePath("CryptoArchive/context-add");
    QDir().mkpath(path);
    return path;
}

QString encodePathLine(const QString& path) {
    return QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString decodePathLine(const QString& line) {
    return QString::fromUtf8(QByteArray::fromBase64(line.trimmed().toLatin1(),
                                                    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool appendContextAddPaths(const QStringList& paths) {
    const QString dir = contextAddQueueDir();
    QLockFile queueLock(QDir(dir).filePath("queue.lock"));
    queueLock.setStaleLockTime(2000);
    if (!queueLock.tryLock(3000)) {
        return false;
    }

    QFile queueFile(QDir(dir).filePath("queue.txt"));
    if (!queueFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&queueFile);
    for (const QString& path : paths) {
        if (!path.trimmed().isEmpty()) {
            out << encodePathLine(QDir::toNativeSeparators(path)) << '\n';
        }
    }

    return true;
}

QStringList takeContextAddPaths() {
    const QString dir = contextAddQueueDir();
    QLockFile queueLock(QDir(dir).filePath("queue.lock"));
    queueLock.setStaleLockTime(2000);
    if (!queueLock.tryLock(3000)) {
        return {};
    }

    QFile queueFile(QDir(dir).filePath("queue.txt"));
    QStringList paths;
    QSet<QString> seen;
    if (queueFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&queueFile);
        while (!in.atEnd()) {
            const QString decoded = decodePathLine(in.readLine());
            const QString normalized = QDir::fromNativeSeparators(decoded);
            if (!normalized.isEmpty() && !seen.contains(normalized)) {
                seen.insert(normalized);
                paths << normalized;
            }
        }
        queueFile.close();
    }
    queueFile.remove();

    return paths;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1 && arguments.at(1) == "--add") {
        QStringList selected;
        for (int i = 2; i < arguments.size(); ++i) {
            selected << arguments.at(i);
        }

        if (!appendContextAddPaths(selected)) {
            return 1;
        }

        auto leaderLock = std::make_unique<QLockFile>(QDir(contextAddQueueDir()).filePath("leader.lock"));
        leaderLock->setStaleLockTime(2500);
        if (!leaderLock->tryLock(0)) {
            return 0;
        }

        cryptsh::ui::MainWindow window;
        window.resize(920, 560);
        QTimer::singleShot(800, &window, [&window]() {
            const QStringList paths = takeContextAddPaths();
            window.show();
            if (!paths.isEmpty()) {
                window.createArchiveFromPaths(paths);
            }
        });

        const int exitCode = QApplication::exec();
        leaderLock.reset();
        return exitCode;
    }

    cryptsh::ui::MainWindow window;
    window.resize(920, 560);
    window.show();

    if (arguments.size() > 1) {
        const QString archivePath = arguments.at(1);
        QTimer::singleShot(0, &window, [&window, archivePath]() {
            window.openArchiveFile(archivePath);
        });
    }

    return QApplication::exec();
}
