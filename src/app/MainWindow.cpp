#include "cryptsh/ui/MainWindow.h"

#include "cryptsh/core/ArchiveError.h"
#include "cryptsh/core/CryptoProvider.h"
#include "cryptsh/core/FileSystemUtils.h"

#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QIcon>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <set>

namespace cryptsh::ui {
namespace {

struct CreateArchiveResult {
    ArchiveMetadata metadata;
    QString error;
};

struct ArchiveRewriteResult {
    ArchiveMetadata metadata;
    QString error;
};

struct ArchiveOperationResult {
    QString error;
};

QString typeName(EntryType type) {
    return type == EntryType::Directory ? "Folder" : "File";
}

QString sizeText(const ArchiveEntry& entry) {
    if (entry.type == EntryType::Directory) {
        return "-";
    }
    constexpr double KiB = 1024.0;
    constexpr double MiB = KiB * 1024.0;
    if (entry.originalSize >= static_cast<std::uint64_t>(MiB)) {
        return QString::number(entry.originalSize / MiB, 'f', 2) + " MB";
    }
    if (entry.originalSize >= static_cast<std::uint64_t>(KiB)) {
        return QString::number(entry.originalSize / KiB, 'f', 2) + " KB";
    }
    return QString::number(entry.originalSize) + " B";
}

std::filesystem::path toFsPath(const QString& value) {
    return std::filesystem::path(value.toStdWString());
}

std::string toUtf8String(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QPushButton* makeButton(QWidget* parent, const QString& tooltip, const QIcon& icon) {
    auto* button = new QPushButton(icon, QString{}, parent);
    button->setMinimumHeight(34);
    button->setFixedWidth(38);
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

bool isDirectChild(const std::string& folder, const std::string& path) {
    std::string rest = path;
    if (!folder.empty()) {
        const std::string prefix = folder + "/";
        if (path.rfind(prefix, 0) != 0) {
            return false;
        }
        rest = path.substr(prefix.size());
    }
    return !rest.empty() && rest.find('/') == std::string::npos;
}

std::string parentFolder(const std::string& folder) {
    const auto slash = folder.find_last_of('/');
    return slash == std::string::npos ? std::string{} : folder.substr(0, slash);
}

QString displayNameFor(const std::string& folder, const std::string& path) {
    std::string rest = path;
    if (!folder.empty()) {
        rest = path.substr(folder.size() + 1);
    }
    return QString::fromUtf8(rest.data(), static_cast<int>(rest.size()));
}

std::filesystem::path uniqueTempDirectory(const std::string& name) {
    return std::filesystem::temp_directory_path()
        / (name + "_" + std::to_string(QDateTime::currentMSecsSinceEpoch()));
}

std::vector<std::filesystem::path> childPaths(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        result.push_back(item.path());
    }
    return result;
}

void replaceFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const auto backup = destination.wstring() + L".bak";
    std::filesystem::remove(backup);
    std::filesystem::rename(destination, backup);
    try {
        std::filesystem::rename(source, destination);
        std::filesystem::remove(backup);
    } catch (...) {
        if (std::filesystem::exists(backup) && !std::filesystem::exists(destination)) {
            std::filesystem::rename(backup, destination);
        }
        throw;
    }
}

void copyIntoDirectory(const std::filesystem::path& source, const std::filesystem::path& destinationRoot) {
    const auto destination = destinationRoot / source.filename();
    std::filesystem::remove_all(destination);
    if (std::filesystem::is_directory(source)) {
        std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive);
    } else {
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildUi();
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);

    auto* toolbar = new QWidget(central);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 12, 12, 12);
    toolbarLayout->setSpacing(8);

    backButton_ = makeButton(toolbar, "Back", style()->standardIcon(QStyle::SP_ArrowBack));
    auto* createFilesButton = makeButton(toolbar, "Create archive from files", style()->standardIcon(QStyle::SP_FileIcon));
    auto* createFolderButton = makeButton(toolbar, "Create archive from folder", style()->standardIcon(QStyle::SP_DirIcon));
    auto* openButton = makeButton(toolbar, "Open .cryptsh archive", style()->standardIcon(QStyle::SP_DialogOpenButton));
    addButton_ = makeButton(toolbar, "Add files or folder to archive", style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    deleteButton_ = makeButton(toolbar, "Delete selected entries", style()->standardIcon(QStyle::SP_TrashIcon));
    extractButton_ = makeButton(toolbar, "Extract all", style()->standardIcon(QStyle::SP_DialogSaveButton));
    extractSelectedButton_ = makeButton(toolbar, "Extract selected", style()->standardIcon(QStyle::SP_ArrowDown));
    viewButton_ = makeButton(toolbar, "View selected file", style()->standardIcon(QStyle::SP_FileDialogContentsView));
    testButton_ = makeButton(toolbar, "Test archive integrity", style()->standardIcon(QStyle::SP_DialogApplyButton));
    infoButton_ = makeButton(toolbar, "Archive info", style()->standardIcon(QStyle::SP_MessageBoxInformation));
    changePasswordButton_ = makeButton(toolbar, "Change archive password", style()->standardIcon(QStyle::SP_DialogResetButton));
    exportKeyButton_ = makeButton(toolbar, "Export my public signing key", style()->standardIcon(QStyle::SP_DriveHDIcon));
    importKeyButton_ = makeButton(toolbar, "Import trusted author public key", style()->standardIcon(QStyle::SP_DirLinkIcon));

    compressionBox_ = new QComboBox(toolbar);
    compressionBox_->addItem("Fast", 1);
    compressionBox_->addItem("Normal", 6);
    compressionBox_->addItem("Maximum", 9);
    compressionBox_->setCurrentIndex(0);
    compressionBox_->setMinimumHeight(34);
    compressionBox_->setToolTip("Compression level for newly created archives.");

    searchEdit_ = new QLineEdit(toolbar);
    searchEdit_->setPlaceholderText("Search in archive");
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setMinimumHeight(34);
    searchEdit_->setMinimumWidth(190);

    toolbarLayout->addWidget(backButton_);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(createFilesButton);
    toolbarLayout->addWidget(createFolderButton);
    toolbarLayout->addWidget(openButton);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(addButton_);
    toolbarLayout->addWidget(deleteButton_);
    toolbarLayout->addWidget(changePasswordButton_);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(extractButton_);
    toolbarLayout->addWidget(extractSelectedButton_);
    toolbarLayout->addWidget(viewButton_);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(testButton_);
    toolbarLayout->addWidget(infoButton_);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(exportKeyButton_);
    toolbarLayout->addWidget(importKeyButton_);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(new QLabel("Compression", toolbar));
    toolbarLayout->addWidget(compressionBox_);
    toolbarLayout->addWidget(searchEdit_);

    table_ = new QTableWidget(central);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Name", "Type", "Size", "CRC32"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->setShowGrid(false);

    status_ = new QLabel("No archive opened", central);
    status_->setObjectName("statusLabel");

    layout->addWidget(toolbar);
    layout->addWidget(table_);
    layout->addWidget(status_);
    setCentralWidget(central);
    setWindowTitle("CryptoArchive (.cryptsh)");
    setWindowIcon(QIcon(":/icons/CryptoArchive.ico"));
    setMinimumSize(1120, 620);
    setArchiveActionsEnabled(false);
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background: #f4f6f8;
            color: #17202a;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QWidget > QWidget {
            border-radius: 8px;
        }
        QPushButton {
            background: #ffffff;
            border: 1px solid #d8dee6;
            border-radius: 7px;
            padding: 6px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #edf4ff;
            border-color: #8bbcff;
        }
        QPushButton:pressed {
            background: #dbeafe;
        }
        QPushButton:disabled {
            color: #9aa4b2;
            background: #eef1f5;
        }
        QLineEdit, QComboBox {
            background: #ffffff;
            border: 1px solid #d8dee6;
            border-radius: 7px;
            padding: 6px 9px;
        }
        QTableWidget {
            background: #ffffff;
            alternate-background-color: #f8fafc;
            border: 1px solid #d8dee6;
            border-radius: 8px;
            selection-background-color: #dbeafe;
            selection-color: #0f172a;
        }
        QHeaderView::section {
            background: #e9eef5;
            border: 0;
            border-bottom: 1px solid #d8dee6;
            padding: 9px;
            font-weight: 700;
        }
        QLabel#statusLabel {
            color: #536173;
            padding-left: 4px;
        }
    )");

    connect(createFilesButton, &QPushButton::clicked, this, &MainWindow::createFilesArchive);
    connect(createFolderButton, &QPushButton::clicked, this, &MainWindow::createFolderArchive);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openArchive);
    connect(extractButton_, &QPushButton::clicked, this, &MainWindow::extractArchive);
    connect(addButton_, &QPushButton::clicked, this, &MainWindow::addToArchive);
    connect(deleteButton_, &QPushButton::clicked, this, &MainWindow::deleteSelectedFromArchive);
    connect(changePasswordButton_, &QPushButton::clicked, this, &MainWindow::changeArchivePassword);
    connect(exportKeyButton_, &QPushButton::clicked, this, &MainWindow::exportPublicKey);
    connect(importKeyButton_, &QPushButton::clicked, this, &MainWindow::importTrustedKey);
    connect(extractSelectedButton_, &QPushButton::clicked, this, &MainWindow::extractSelectedArchive);
    connect(viewButton_, &QPushButton::clicked, this, &MainWindow::viewSelectedFile);
    connect(testButton_, &QPushButton::clicked, this, &MainWindow::testArchive);
    connect(infoButton_, &QPushButton::clicked, this, &MainWindow::showArchiveInfo);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::filterEntries);
    connect(table_, &QTableWidget::cellDoubleClicked, this, &MainWindow::openTableItem);
    connect(backButton_, &QPushButton::clicked, this, &MainWindow::goBack);
}

QString MainWindow::requestPassword(const QString& title) {
    bool ok = false;
    const QString password = QInputDialog::getText(
        this,
        title,
        "Password:",
        QLineEdit::Password,
        {},
        &ok);
    return ok ? password : QString{};
}

void MainWindow::createFilesArchive() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Choose files to archive");
    createArchiveFromSelection(files);
}

void MainWindow::createFolderArchive() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Choose folder to archive");
    if (folder.isEmpty()) {
        return;
    }
    createArchiveFromSelection({folder});
}

void MainWindow::createArchiveFromPaths(const QStringList& selected) {
    createArchiveFromSelection(selected);
}

void MainWindow::createArchiveFromSelection(const QStringList& selected) {
    if (selected.isEmpty()) {
        return;
    }
    QString archivePath = QFileDialog::getSaveFileName(
        this,
        "Save .cryptsh archive",
        {},
        "CryptoArchive (*.cryptsh)");
    if (archivePath.isEmpty()) {
        return;
    }
    if (!archivePath.endsWith(".cryptsh", Qt::CaseInsensitive)) {
        archivePath += ".cryptsh";
    }

    const QString password = requestPassword("Create encrypted archive");
    if (password.isEmpty()) {
        return;
    }

    std::vector<std::filesystem::path> paths;
    for (const auto& item : selected) {
        paths.emplace_back(toFsPath(item));
    }

    ArchiveOptions options;
    options.compressionLevel = compressionBox_->currentData().toInt();
    options.kdfIterations = options_.kdfIterations;

    auto* progress = new QProgressDialog("Preparing archive...", QString{}, 0, 100, this);
    progress->setWindowTitle("Creating .cryptsh archive");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->setValue(0);
    progress->setStyleSheet(R"(
        QProgressDialog {
            background: #f4f6f8;
            color: #17202a;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QProgressBar {
            border: 1px solid #cbd5e1;
            border-radius: 7px;
            background: #e9eef5;
            height: 18px;
            text-align: center;
            font-weight: 700;
        }
        QProgressBar::chunk {
            border-radius: 7px;
            background: #2563eb;
        }
    )");
    progress->show();

    auto* watcher = new QFutureWatcher<CreateArchiveResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const QString archivePathCopy = archivePath;
    const std::string passwordCopy = toUtf8String(password);
    const auto archiveFsPath = toFsPath(archivePath);

    connect(watcher, &QFutureWatcher<CreateArchiveResult>::finished, this, [this, watcher, progressGuard, archivePathCopy]() {
        const CreateArchiveResult result = watcher->result();
        watcher->deleteLater();

        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->setLabelText("Archive created.\n100%");
            progressGuard->close();
        }

        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            status_->setText("Archive creation failed.");
            return;
        }

        currentArchive_ = archivePathCopy;
        metadata_ = result.metadata;
        currentFolder_.clear();
        showEntries(metadata_);
        status_->setText("Created: " + archivePathCopy);
        setArchiveActionsEnabled(true);
    });

    auto future = QtConcurrent::run([paths = std::move(paths), archiveFsPath, passwordCopy, options, progressGuard]() mutable {
        CreateArchiveResult result;
        ArchiveService workerService;
        options.progress = [progressGuard](int current, int total, const std::string& message) {
            const int safeTotal = std::max(total, 1);
            const int percent = std::clamp((current * 100) / safeTotal, 0, 100);
            const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                + "\n" + QString::number(percent) + "%";
            QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                if (!progressGuard) {
                    return;
                }
                progressGuard->setLabelText(label);
                progressGuard->setValue(percent);
            }, Qt::QueuedConnection);
        };

        try {
            workerService.createArchive(paths, archiveFsPath, passwordCopy, options);
            result.metadata = workerService.openArchive(archiveFsPath, passwordCopy);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::openArchive() {
    const QString archivePath = QFileDialog::getOpenFileName(
        this,
        "Open .cryptsh archive",
        {},
        "CryptoArchive (*.cryptsh)");
    if (archivePath.isEmpty()) {
        return;
    }

    openArchiveFile(archivePath);
}

void MainWindow::openArchiveFile(const QString& archivePath) {
    if (archivePath.isEmpty()) {
        return;
    }
    if (!QFileInfo::exists(archivePath)) {
        QMessageBox::critical(this, "Archive error", "Archive file does not exist:\n" + archivePath);
        return;
    }

    const QString password = requestPassword("Open encrypted archive");
    if (password.isEmpty()) {
        return;
    }

    try {
        metadata_ = service_.openArchive(toFsPath(archivePath), toUtf8String(password));
        currentArchive_ = archivePath;
        currentFolder_.clear();
        showEntries(metadata_);
        status_->setText("Opened: " + archivePath);
        setArchiveActionsEnabled(true);
    } catch (const ArchiveError& error) {
        QMessageBox::critical(this, "Archive error", error.what());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Unexpected error", error.what());
    }
}

void MainWindow::extractSelectedArchive() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const auto selected = selectedArchivePaths();
    if (selected.empty()) {
        QMessageBox::information(this, "Nothing selected", "Select one or more entries in the archive table.");
        return;
    }

    const QString destination = QFileDialog::getExistingDirectory(this, "Extract selected entries to");
    if (destination.isEmpty()) {
        return;
    }

    const QString password = requestPassword("Extract selected entries");
    if (password.isEmpty()) {
        return;
    }

    auto* progress = new QProgressDialog("Preparing extraction...", QString{}, 0, 100, this);
    progress->setWindowTitle("Extracting selected entries");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->setValue(0);
    progress->setStyleSheet(R"(
        QProgressDialog {
            background: #f4f6f8;
            color: #17202a;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QProgressBar {
            border: 1px solid #cbd5e1;
            border-radius: 7px;
            background: #e9eef5;
            height: 18px;
            text-align: center;
            font-weight: 700;
        }
        QProgressBar::chunk {
            border-radius: 7px;
            background: #2563eb;
        }
    )");
    progress->show();

    auto* watcher = new QFutureWatcher<ArchiveOperationResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const auto archivePath = toFsPath(currentArchive_);
    const auto destinationPath = toFsPath(destination);
    const std::string passwordCopy = toUtf8String(password);
    const QString destinationText = destination;

    connect(watcher, &QFutureWatcher<ArchiveOperationResult>::finished, this, [this, watcher, progressGuard, destinationText]() {
        const ArchiveOperationResult result = watcher->result();
        watcher->deleteLater();
        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->close();
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            status_->setText("Extraction failed.");
            return;
        }
        status_->setText("Extracted selected entries to: " + destinationText);
        QMessageBox::information(this, "Done", "Selected entries extracted successfully.");
    });

    auto future = QtConcurrent::run([archivePath, destinationPath, passwordCopy, selected, progressGuard]() mutable {
        ArchiveOperationResult result;
        ArchiveOptions options;
        options.progress = [progressGuard](int current, int total, const std::string& message) {
            const int safeTotal = std::max(total, 1);
            const int percent = std::clamp((current * 100) / safeTotal, 0, 100);
            const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                + "\n" + QString::number(percent) + "%";
            QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                if (progressGuard) {
                    progressGuard->setLabelText(label);
                    progressGuard->setValue(percent);
                }
            }, Qt::QueuedConnection);
        };
        try {
            ArchiveService().extractSelected(archivePath, destinationPath, passwordCopy, selected, options);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::testArchive() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const QString password = requestPassword("Test archive integrity");
    if (password.isEmpty()) {
        return;
    }

    try {
        service_.testArchive(toFsPath(currentArchive_), toUtf8String(password));
        status_->setText("Archive test passed: " + currentArchive_);
        QMessageBox::information(this, "Archive test", "Digital signature, password, AES-GCM tag, decompression, and CRC32 checks passed.");
    } catch (const ArchiveError& error) {
        QMessageBox::critical(this, "Archive error", error.what());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Unexpected error", error.what());
    }
}

void MainWindow::showArchiveInfo() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    std::uint64_t originalSize = 0;
    std::uint64_t storedSize = 0;
    int fileCount = 0;
    int folderCount = 0;
    for (const auto& entry : metadata_.entries) {
        if (entry.type == EntryType::File) {
            ++fileCount;
            originalSize += entry.originalSize;
            storedSize += entry.storedSize;
        } else if (entry.type == EntryType::Directory) {
            ++folderCount;
        }
    }

    const auto archiveSize = std::filesystem::exists(toFsPath(currentArchive_))
        ? std::filesystem::file_size(toFsPath(currentArchive_))
        : 0;
    const auto signature = CryptoProvider().signatureInfo(toFsPath(currentArchive_));
    const QString signatureText = !signature.present
        ? "Not present"
        : (!signature.valid
            ? "Invalid"
            : (signature.trusted
                ? "Ed25519 valid, trusted author: " + QString::fromUtf8(signature.author.data(), static_cast<int>(signature.author.size()))
                : "Ed25519 valid, unknown author, fingerprint: " + QString::fromStdString(signature.fingerprint)));
    const double payloadRatio = originalSize == 0
        ? 0.0
        : (1.0 - static_cast<double>(storedSize) / static_cast<double>(originalSize)) * 100.0;
    const double finalRatio = originalSize == 0
        ? 0.0
        : (1.0 - static_cast<double>(archiveSize) / static_cast<double>(originalSize)) * 100.0;

    QMessageBox::information(
        this,
        "Archive info",
        "Archive: " + currentArchive_ + "\n"
        "Files: " + QString::number(fileCount) + "\n"
        "Folders: " + QString::number(folderCount) + "\n"
        "Original size: " + QString::number(originalSize) + " B\n"
        "Compressed payload size: " + QString::number(storedSize) + " B\n"
        "Final encrypted size: " + QString::number(archiveSize) + " B\n"
        "Payload compression: " + QString::number(payloadRatio, 'f', 2) + "%\n"
        "Final ratio: " + QString::number(finalRatio, 'f', 2) + "%\n"
        "Encryption: AES-256-GCM\n"
        "KDF: PBKDF2-SHA256\n"
        "Digital signature: " + signatureText);
}

void MainWindow::viewSelectedFile() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const auto ranges = table_->selectedRanges();
    if (ranges.size() != 1 || ranges.first().rowCount() != 1) {
        QMessageBox::information(this, "Choose one file", "Select exactly one file to preview.");
        return;
    }

    const int row = ranges.first().topRow();
    if (row < 0 || row >= static_cast<int>(visibleEntries_.size())) {
        return;
    }
    const auto& entry = metadata_.entries[visibleEntries_[static_cast<std::size_t>(row)]];
    if (entry.type != EntryType::File) {
        QMessageBox::information(this, "Choose a file", "Folders cannot be opened in preview mode.");
        return;
    }

    const QString password = requestPassword("View archived file");
    if (password.isEmpty()) {
        return;
    }

    const QString previewRoot = QDir::tempPath() + "/CryptoArchivePreview_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    try {
        service_.extractSelected(toFsPath(currentArchive_), toFsPath(previewRoot), toUtf8String(password), {entry.path});
        const auto previewPath = toFsPath(previewRoot) / fsutils::pathFromArchiveUtf8(entry.path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(previewPath.wstring())));
        status_->setText("Opened preview: " + QString::fromUtf8(entry.path.data(), static_cast<int>(entry.path.size())));
    } catch (const ArchiveError& error) {
        QMessageBox::critical(this, "Archive error", error.what());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Unexpected error", error.what());
    }
}

void MainWindow::addToArchive() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    QStringList selected = QFileDialog::getOpenFileNames(this, "Choose files to add");
    if (selected.isEmpty()) {
        const QString folder = QFileDialog::getExistingDirectory(this, "Or choose a folder to add");
        if (!folder.isEmpty()) {
            selected << folder;
        }
    }
    if (selected.isEmpty()) {
        return;
    }

    const QString password = requestPassword("Unlock archive");
    if (password.isEmpty()) {
        return;
    }

    std::vector<std::filesystem::path> additions;
    for (const auto& item : selected) {
        additions.push_back(toFsPath(item));
    }

    ArchiveOptions options;
    options.compressionLevel = compressionBox_->currentData().toInt();
    options.kdfIterations = options_.kdfIterations;

    auto* progress = new QProgressDialog("Adding files...", QString{}, 0, 100, this);
    progress->setWindowTitle("Updating .cryptsh archive");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->show();

    auto* watcher = new QFutureWatcher<ArchiveRewriteResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const auto archivePath = toFsPath(currentArchive_);
    const QString archiveText = currentArchive_;
    const std::string passwordCopy = toUtf8String(password);
    const std::string targetFolder = currentFolder_;

    connect(watcher, &QFutureWatcher<ArchiveRewriteResult>::finished, this, [this, watcher, progressGuard, archiveText]() {
        const ArchiveRewriteResult result = watcher->result();
        watcher->deleteLater();
        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->close();
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            return;
        }
        currentArchive_ = archiveText;
        metadata_ = result.metadata;
        showEntries(metadata_);
        setArchiveActionsEnabled(true);
        status_->setText("Archive updated: " + archiveText);
    });

    auto future = QtConcurrent::run([archivePath, additions = std::move(additions), passwordCopy, options, progressGuard, targetFolder]() mutable {
        ArchiveRewriteResult result;
        ArchiveService workerService;
        const auto tempRoot = uniqueTempDirectory("CryptoArchiveEdit");
        const auto tempArchive = archivePath.wstring() + L".tmp.cryptsh";
        try {
            std::filesystem::create_directories(tempRoot);
            QMetaObject::invokeMethod(qApp, [progressGuard]() {
                if (progressGuard) {
                    progressGuard->setLabelText("Decrypting archive...");
                    progressGuard->setValue(5);
                }
            }, Qt::QueuedConnection);
            workerService.extractArchive(archivePath, tempRoot, passwordCopy);

            auto destinationRoot = tempRoot;
            if (!targetFolder.empty()) {
                destinationRoot /= fsutils::pathFromArchiveUtf8(targetFolder);
                std::filesystem::create_directories(destinationRoot);
            }
            for (const auto& item : additions) {
                copyIntoDirectory(item, destinationRoot);
            }

            auto inputs = childPaths(tempRoot);
            if (inputs.empty()) {
                throw ArchiveError("Archive cannot be empty.");
            }
            options.progress = [progressGuard](int current, int total, const std::string& message) {
                const int safeTotal = std::max(total, 1);
                const int percent = std::clamp(10 + (current * 85) / safeTotal, 10, 95);
                const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                    + "\n" + QString::number(percent) + "%";
                QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                    if (progressGuard) {
                        progressGuard->setLabelText(label);
                        progressGuard->setValue(percent);
                    }
                }, Qt::QueuedConnection);
            };
            workerService.createArchive(inputs, tempArchive, passwordCopy, options);
            replaceFile(tempArchive, archivePath);
            result.metadata = workerService.openArchive(archivePath, passwordCopy);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
            std::filesystem::remove(tempArchive);
        }
        std::filesystem::remove_all(tempRoot);
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::deleteSelectedFromArchive() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const auto selected = selectedArchivePaths();
    if (selected.empty()) {
        QMessageBox::information(this, "Nothing selected", "Select entries to delete from the archive.");
        return;
    }
    if (QMessageBox::question(this, "Delete from archive", "Delete selected entries from this archive?") != QMessageBox::Yes) {
        return;
    }

    const QString password = requestPassword("Unlock archive");
    if (password.isEmpty()) {
        return;
    }

    ArchiveOptions options;
    options.compressionLevel = compressionBox_->currentData().toInt();
    options.kdfIterations = options_.kdfIterations;

    auto* progress = new QProgressDialog("Deleting entries...", QString{}, 0, 100, this);
    progress->setWindowTitle("Updating .cryptsh archive");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->show();

    auto* watcher = new QFutureWatcher<ArchiveRewriteResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const auto archivePath = toFsPath(currentArchive_);
    const QString archiveText = currentArchive_;
    const std::string passwordCopy = toUtf8String(password);

    connect(watcher, &QFutureWatcher<ArchiveRewriteResult>::finished, this, [this, watcher, progressGuard, archiveText]() {
        const ArchiveRewriteResult result = watcher->result();
        watcher->deleteLater();
        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->close();
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            return;
        }
        currentArchive_ = archiveText;
        metadata_ = result.metadata;
        currentFolder_.clear();
        showEntries(metadata_);
        setArchiveActionsEnabled(true);
        status_->setText("Archive updated: " + archiveText);
    });

    auto future = QtConcurrent::run([archivePath, selected, passwordCopy, options, progressGuard]() mutable {
        ArchiveRewriteResult result;
        ArchiveService workerService;
        const auto tempRoot = uniqueTempDirectory("CryptoArchiveDelete");
        const auto tempArchive = archivePath.wstring() + L".tmp.cryptsh";
        try {
            std::filesystem::create_directories(tempRoot);
            QMetaObject::invokeMethod(qApp, [progressGuard]() {
                if (progressGuard) {
                    progressGuard->setLabelText("Decrypting archive...");
                    progressGuard->setValue(5);
                }
            }, Qt::QueuedConnection);
            workerService.extractArchive(archivePath, tempRoot, passwordCopy);

            for (const auto& item : selected) {
                std::filesystem::remove_all(tempRoot / fsutils::pathFromArchiveUtf8(item));
            }

            auto inputs = childPaths(tempRoot);
            if (inputs.empty()) {
                throw ArchiveError("Archive cannot be empty.");
            }
            options.progress = [progressGuard](int current, int total, const std::string& message) {
                const int safeTotal = std::max(total, 1);
                const int percent = std::clamp(10 + (current * 85) / safeTotal, 10, 95);
                const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                    + "\n" + QString::number(percent) + "%";
                QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                    if (progressGuard) {
                        progressGuard->setLabelText(label);
                        progressGuard->setValue(percent);
                    }
                }, Qt::QueuedConnection);
            };
            workerService.createArchive(inputs, tempArchive, passwordCopy, options);
            replaceFile(tempArchive, archivePath);
            result.metadata = workerService.openArchive(archivePath, passwordCopy);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
            std::filesystem::remove(tempArchive);
        }
        std::filesystem::remove_all(tempRoot);
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::changeArchivePassword() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const QString oldPassword = requestPassword("Current password");
    if (oldPassword.isEmpty()) {
        return;
    }
    const QString newPassword = requestPassword("New password");
    if (newPassword.isEmpty()) {
        return;
    }

    ArchiveOptions options;
    options.compressionLevel = compressionBox_->currentData().toInt();
    options.kdfIterations = options_.kdfIterations;

    auto* progress = new QProgressDialog("Changing password...", QString{}, 0, 100, this);
    progress->setWindowTitle("Re-encrypting .cryptsh archive");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->show();

    auto* watcher = new QFutureWatcher<ArchiveRewriteResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const auto archivePath = toFsPath(currentArchive_);
    const QString archiveText = currentArchive_;
    const std::string oldPasswordCopy = toUtf8String(oldPassword);
    const std::string newPasswordCopy = toUtf8String(newPassword);

    connect(watcher, &QFutureWatcher<ArchiveRewriteResult>::finished, this, [this, watcher, progressGuard, archiveText]() {
        const ArchiveRewriteResult result = watcher->result();
        watcher->deleteLater();
        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->close();
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            return;
        }
        currentArchive_ = archiveText;
        metadata_ = result.metadata;
        showEntries(metadata_);
        setArchiveActionsEnabled(true);
        status_->setText("Password changed: " + archiveText);
    });

    auto future = QtConcurrent::run([archivePath, oldPasswordCopy, newPasswordCopy, options, progressGuard]() mutable {
        ArchiveRewriteResult result;
        ArchiveService workerService;
        const auto tempRoot = uniqueTempDirectory("CryptoArchivePassword");
        const auto tempArchive = archivePath.wstring() + L".tmp.cryptsh";
        try {
            std::filesystem::create_directories(tempRoot);
            QMetaObject::invokeMethod(qApp, [progressGuard]() {
                if (progressGuard) {
                    progressGuard->setLabelText("Decrypting archive...");
                    progressGuard->setValue(5);
                }
            }, Qt::QueuedConnection);
            workerService.extractArchive(archivePath, tempRoot, oldPasswordCopy);

            auto inputs = childPaths(tempRoot);
            if (inputs.empty()) {
                throw ArchiveError("Archive cannot be empty.");
            }
            options.progress = [progressGuard](int current, int total, const std::string& message) {
                const int safeTotal = std::max(total, 1);
                const int percent = std::clamp(10 + (current * 85) / safeTotal, 10, 95);
                const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                    + "\n" + QString::number(percent) + "%";
                QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                    if (progressGuard) {
                        progressGuard->setLabelText(label);
                        progressGuard->setValue(percent);
                    }
                }, Qt::QueuedConnection);
            };
            workerService.createArchive(inputs, tempArchive, newPasswordCopy, options);
            replaceFile(tempArchive, archivePath);
            result.metadata = workerService.openArchive(archivePath, newPasswordCopy);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
            std::filesystem::remove(tempArchive);
        }
        std::filesystem::remove_all(tempRoot);
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::exportPublicKey() {
    bool ok = false;
    const QString author = QInputDialog::getText(
        this,
        "Export public key",
        "Author name:",
        QLineEdit::Normal,
        {},
        &ok);
    if (!ok || author.isEmpty()) {
        return;
    }

    QString outputPath = QFileDialog::getSaveFileName(
        this,
        "Save public key",
        author + ".cshpub",
        "CryptoArchive public key (*.cshpub)");
    if (outputPath.isEmpty()) {
        return;
    }
    if (!outputPath.endsWith(".cshpub", Qt::CaseInsensitive)) {
        outputPath += ".cshpub";
    }

    try {
        CryptoProvider().exportPublicKey(toFsPath(outputPath), toUtf8String(author));
        QMessageBox::information(this, "Public key exported", "Public key saved:\n" + outputPath);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Key error", error.what());
    }
}

void MainWindow::importTrustedKey() {
    const QString keyPath = QFileDialog::getOpenFileName(
        this,
        "Import trusted public key",
        {},
        "CryptoArchive public key (*.cshpub)");
    if (keyPath.isEmpty()) {
        return;
    }

    try {
        CryptoProvider().importTrustedPublicKey(toFsPath(keyPath));
        QMessageBox::information(this, "Trusted key imported", "Trusted author key imported successfully.");
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "Key error", error.what());
    }
}

void MainWindow::extractArchive() {
    if (currentArchive_.isEmpty()) {
        return;
    }

    const QString destination = QFileDialog::getExistingDirectory(this, "Extract archive to");
    if (destination.isEmpty()) {
        return;
    }

    const QString password = requestPassword("Extract encrypted archive");
    if (password.isEmpty()) {
        return;
    }

    auto* progress = new QProgressDialog("Preparing extraction...", QString{}, 0, 100, this);
    progress->setWindowTitle("Extracting .cryptsh archive");
    progress->setWindowModality(Qt::NonModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    progress->setValue(0);
    progress->setStyleSheet(R"(
        QProgressDialog {
            background: #f4f6f8;
            color: #17202a;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QProgressBar {
            border: 1px solid #cbd5e1;
            border-radius: 7px;
            background: #e9eef5;
            height: 18px;
            text-align: center;
            font-weight: 700;
        }
        QProgressBar::chunk {
            border-radius: 7px;
            background: #2563eb;
        }
    )");
    progress->show();

    auto* watcher = new QFutureWatcher<ArchiveOperationResult>(this);
    const QPointer<QProgressDialog> progressGuard(progress);
    const auto archivePath = toFsPath(currentArchive_);
    const auto destinationPath = toFsPath(destination);
    const std::string passwordCopy = toUtf8String(password);
    const QString destinationText = destination;

    connect(watcher, &QFutureWatcher<ArchiveOperationResult>::finished, this, [this, watcher, progressGuard, destinationText]() {
        const ArchiveOperationResult result = watcher->result();
        watcher->deleteLater();
        if (progressGuard) {
            progressGuard->setValue(100);
            progressGuard->close();
        }
        if (!result.error.isEmpty()) {
            QMessageBox::critical(this, "Archive error", result.error);
            status_->setText("Extraction failed.");
            return;
        }
        status_->setText("Extracted to: " + destinationText);
        QMessageBox::information(this, "Done", "Archive extracted successfully.");
    });

    auto future = QtConcurrent::run([archivePath, destinationPath, passwordCopy, progressGuard]() mutable {
        ArchiveOperationResult result;
        ArchiveOptions options;
        options.progress = [progressGuard](int current, int total, const std::string& message) {
            const int safeTotal = std::max(total, 1);
            const int percent = std::clamp((current * 100) / safeTotal, 0, 100);
            const QString label = QString::fromUtf8(message.data(), static_cast<int>(message.size()))
                + "\n" + QString::number(percent) + "%";
            QMetaObject::invokeMethod(qApp, [progressGuard, percent, label]() {
                if (progressGuard) {
                    progressGuard->setLabelText(label);
                    progressGuard->setValue(percent);
                }
            }, Qt::QueuedConnection);
        };
        try {
            ArchiveService().extractArchive(archivePath, destinationPath, passwordCopy, options);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    });
    watcher->setFuture(future);
}

void MainWindow::showEntries(const ArchiveMetadata& metadata) {
    metadata_ = metadata;
    displayCurrentFolder();
}

void MainWindow::displayCurrentFolder() {
    QFileIconProvider icons;
    visibleEntries_.clear();
    table_->setRowCount(0);

    for (std::size_t index = 0; index < metadata_.entries.size(); ++index) {
        const auto& entry = metadata_.entries[index];
        if (!isDirectChild(currentFolder_, entry.path)) {
            continue;
        }

        const int row = table_->rowCount();
        table_->insertRow(row);
        visibleEntries_.push_back(index);
        auto* nameItem = new QTableWidgetItem(displayNameFor(currentFolder_, entry.path));
        nameItem->setIcon(icons.icon(entry.type == EntryType::Directory ? QFileIconProvider::Folder : QFileIconProvider::File));
        table_->setItem(row, 0, nameItem);
        table_->setItem(row, 1, new QTableWidgetItem(typeName(entry.type)));
        table_->setItem(row, 2, new QTableWidgetItem(sizeText(entry)));
        table_->setItem(row, 3, new QTableWidgetItem(QString::number(entry.crc32, 16).rightJustified(8, '0')));
    }
    backButton_->setEnabled(!currentFolder_.empty());
    const QString place = currentFolder_.empty()
        ? QString("Archive root")
        : QString::fromUtf8(currentFolder_.data(), static_cast<int>(currentFolder_.size()));
    status_->setText(currentArchive_.isEmpty() ? place : place + " - " + currentArchive_);
    filterEntries(searchEdit_ ? searchEdit_->text() : QString{});
}

void MainWindow::setArchiveActionsEnabled(bool enabled) {
    extractButton_->setEnabled(enabled);
    extractSelectedButton_->setEnabled(enabled);
    testButton_->setEnabled(enabled);
    infoButton_->setEnabled(enabled);
    viewButton_->setEnabled(enabled);
    addButton_->setEnabled(enabled);
    deleteButton_->setEnabled(enabled);
    changePasswordButton_->setEnabled(enabled);
    backButton_->setEnabled(enabled && !currentFolder_.empty());
}

std::vector<std::string> MainWindow::selectedArchivePaths() const {
    std::vector<std::string> paths;
    std::set<int> rows;
    for (const auto& range : table_->selectedRanges()) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.insert(row);
        }
    }
    for (int row : rows) {
        if (row >= 0 && row < static_cast<int>(visibleEntries_.size())) {
            paths.push_back(metadata_.entries[visibleEntries_[static_cast<std::size_t>(row)]].path);
        }
    }
    return paths;
}

void MainWindow::filterEntries(const QString& text) {
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, 0);
        const bool visible = text.isEmpty() || (item && item->text().contains(text, Qt::CaseInsensitive));
        table_->setRowHidden(row, !visible);
    }
}

void MainWindow::openTableItem(int row, int) {
    if (row < 0 || row >= static_cast<int>(visibleEntries_.size())) {
        return;
    }
    const auto& entry = metadata_.entries[visibleEntries_[static_cast<std::size_t>(row)]];
    if (entry.type != EntryType::Directory) {
        return;
    }
    currentFolder_ = entry.path;
    displayCurrentFolder();
}

void MainWindow::goBack() {
    if (currentFolder_.empty()) {
        return;
    }
    currentFolder_ = parentFolder(currentFolder_);
    displayCurrentFolder();
}

} 
