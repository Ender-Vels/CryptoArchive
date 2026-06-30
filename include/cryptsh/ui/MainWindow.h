#pragma once

#include "cryptsh/core/ArchiveEntry.h"
#include "cryptsh/core/ArchiveService.h"

#include <QMainWindow>

class QLabel;
class QPushButton;
class QComboBox;
class QLineEdit;
class QTableWidget;

namespace cryptsh::ui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openArchiveFile(const QString& archivePath);
    void createArchiveFromPaths(const QStringList& selected);

private slots:
    void createFilesArchive();
    void createFolderArchive();
    void openArchive();
    void extractArchive();
    void extractSelectedArchive();
    void testArchive();
    void showArchiveInfo();
    void viewSelectedFile();
    void addToArchive();
    void deleteSelectedFromArchive();
    void changeArchivePassword();
    void exportPublicKey();
    void importTrustedKey();
    void filterEntries(const QString& text);
    void openTableItem(int row, int column);
    void goBack();

private:
    void buildUi();
    void createArchiveFromSelection(const QStringList& selected);
    void showEntries(const ArchiveMetadata& metadata);
    void displayCurrentFolder();
    void setArchiveActionsEnabled(bool enabled);
    std::vector<std::string> selectedArchivePaths() const;
    QString requestPassword(const QString& title);

    ArchiveService service_;
    ArchiveMetadata metadata_;
    ArchiveOptions options_;
    std::vector<std::size_t> visibleEntries_;
    std::string currentFolder_;
    QString currentArchive_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* compressionBox_ = nullptr;
    QPushButton* extractButton_ = nullptr;
    QPushButton* extractSelectedButton_ = nullptr;
    QPushButton* testButton_ = nullptr;
    QPushButton* infoButton_ = nullptr;
    QPushButton* viewButton_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* changePasswordButton_ = nullptr;
    QPushButton* exportKeyButton_ = nullptr;
    QPushButton* importKeyButton_ = nullptr;
};

} // namespace cryptsh::ui
