#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void selectISO();
    void createPartition();
    void copyISO();
    void configureBCD();
    void openServicesPage();

private:
    void setupUI();
    void applyStyles();

    // Header
    QFrame *headerFrame;
    QLabel *logoLabel;
    QLabel *titleLabel;
    QLabel *subtitleLabel;
    QPushButton *saveButton;

    // Central
    QWidget *centralWidget;
    QVBoxLayout *centralLayout;
    QLabel *isoPathLabel;
    QLineEdit *isoPathEdit;
    QPushButton *browseButton;
    QPushButton *createPartitionButton;
    QPushButton *copyISOButton;
    QPushButton *configureBCDButton;
    QTextEdit *logTextEdit;

    // Footer
    QFrame *footerFrame;
    QLabel *footerLabel;
    QPushButton *servicesButton;
};

#endif // MAINWINDOW_H