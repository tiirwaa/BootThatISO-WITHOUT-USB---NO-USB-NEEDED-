#include "mainwindow.h"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>
#include <QIcon>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    partitionManager = new PartitionManager();
    isoCopyManager = new ISOCopyManager();
    bcdManager = new BCDManager();
    setupUI();
    applyStyles();
    setWindowTitle("Easy ISOBoot - Configuración de Partición Bootable");
    setFixedSize(800, 600);
    setWindowIcon(QIcon(":/resources/logo.ico")); // Placeholder, necesitarás agregar recursos
}

MainWindow::~MainWindow()
{
    delete partitionManager;
    delete isoCopyManager;
    delete bcdManager;
}

void MainWindow::setupUI()
{
    // Central widget
    centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    centralLayout = new QVBoxLayout(centralWidget);

    // Header Frame
    headerFrame = new QFrame;
    headerFrame->setFrameStyle(QFrame::Box);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerFrame);

    // Logo placeholder
    logoLabel = new QLabel;
    // logoLabel->setPixmap(QPixmap(":/resources/logo.png").scaled(55, 55)); // Placeholder
    logoLabel->setText("LOGO"); // Placeholder
    logoLabel->setFixedSize(55, 55);
    logoLabel->setStyleSheet("background-color: #2A2A2A; color: white; font-weight: bold;");
    headerLayout->addWidget(logoLabel);

    // Title and subtitle
    QVBoxLayout *titleLayout = new QVBoxLayout;
    titleLabel = new QLabel("EASY ISOBOOT");
    titleLabel->setStyleSheet("font-size: 18pt; font-weight: bold; color: #42A5F5;");
    subtitleLabel = new QLabel("Configuración de Partición Bootable");
    subtitleLabel->setStyleSheet("font-size: 10pt; color: #B0B0B0;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(subtitleLabel);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();

    // Save button
    saveButton = new QPushButton(" Guardar Config");
    saveButton->setStyleSheet("background-color: #27AE60; color: white; padding: 8px 12px;");
    headerLayout->addWidget(saveButton);

    centralLayout->addWidget(headerFrame);

    // Central content
    isoPathLabel = new QLabel("Ruta del archivo ISO:");
    centralLayout->addWidget(isoPathLabel);

    QHBoxLayout *isoLayout = new QHBoxLayout;
    isoPathEdit = new QLineEdit;
    browseButton = new QPushButton("Buscar");
    browseButton->setStyleSheet("background-color: #6200EE; color: white;");
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::selectISO);
    isoLayout->addWidget(isoPathEdit);
    isoLayout->addWidget(browseButton);
    centralLayout->addLayout(isoLayout);

    // Disk space info
    diskSpaceLabel = new QLabel;
    updateDiskSpaceInfo();
    centralLayout->addWidget(diskSpaceLabel);

    createPartitionButton = new QPushButton("Realizar proceso y Bootear ISO seleccionado");
    createPartitionButton->setStyleSheet("background-color: blue; color: white; padding: 10px;");
    connect(createPartitionButton, &QPushButton::clicked, this, &MainWindow::createPartition);
    centralLayout->addWidget(createPartitionButton);

    logTextEdit = new QTextEdit;
    logTextEdit->setPlaceholderText("Logs de operaciones...");
    centralLayout->addWidget(logTextEdit);

    // Footer Frame
    footerFrame = new QFrame;
    footerFrame->setFrameStyle(QFrame::Box);
    QHBoxLayout *footerLayout = new QHBoxLayout(footerFrame);

    footerLabel = new QLabel("Versión 1.0");
    footerLabel->setStyleSheet("color: #808080;");
    footerLayout->addWidget(footerLabel);
    footerLayout->addStretch();

    servicesButton = new QPushButton("Servicios");
    servicesButton->setStyleSheet("color: #42A5F5; border: none;");
    connect(servicesButton, &QPushButton::clicked, this, &MainWindow::openServicesPage);
    footerLayout->addWidget(servicesButton);

    centralLayout->addWidget(footerFrame);
}

void MainWindow::applyStyles()
{
    setStyleSheet(R"(
        MainWindow {
            background-color: #1E1E1E;
            color: #EEE;
        }
        QLabel {
            color: #EEE;
        }
        QLineEdit {
            background-color: #2A2A2A;
            color: #EEE;
            border: 1px solid #555;
            padding: 5px;
        }
        QPushButton {
            border: none;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            opacity: 0.8;
        }
        QTextEdit {
            background-color: #2A2A2A;
            color: #EEE;
            border: 1px solid #555;
        }
        QFrame {
            background-color: #1E1E1E;
            border: 1px solid #42A5F5;
        }
    )");
}

void MainWindow::selectISO()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Seleccionar archivo ISO", "", "Archivos ISO (*.iso)");
    if (!fileName.isEmpty()) {
        isoPathEdit->setText(fileName);
    }
}

void MainWindow::createPartition()
{
    // Validación de espacio disponible usando el partitionManager
    SpaceValidationResult validation = partitionManager->validateAvailableSpace();
    if (!validation.isValid) {
        QMessageBox::critical(this, "Espacio Insuficiente", validation.errorMessage);
        return;
    }

    // Primera alerta de confirmación
    QMessageBox::StandardButton reply1 = QMessageBox::question(this, "Confirmación de Operación",
        "Esta operación modificará el disco del sistema, reduciendo su tamaño en 10 GB para crear una partición bootable. ¿Desea continuar?",
        QMessageBox::Yes | QMessageBox::No);
    if (reply1 != QMessageBox::Yes) {
        return;
    }

    // Segunda alerta de confirmación
    QMessageBox::StandardButton reply2 = QMessageBox::question(this, "Segunda Confirmación",
        "Esta es la segunda confirmación. La operación de modificación del disco es irreversible y puede causar pérdida de datos si no se realiza correctamente. ¿Está completamente seguro de que desea proceder?",
        QMessageBox::Yes | QMessageBox::No);
    if (reply2 != QMessageBox::Yes) {
        return;
    }

    // Llamar al partitionManager para crear la partición
    if (partitionManager->createPartition()) {
        // Proceder con la copia del ISO
        copyISO();
        // Luego configurar BCD
        configureBCD();
    } else {
        QMessageBox::critical(this, "Error", "Error al crear la partición.");
    }
}

void MainWindow::copyISO()
{
    QString isoPath = isoPathEdit->text();
    if (isoPath.isEmpty()) {
        QMessageBox::warning(this, "Archivo ISO", "Por favor, seleccione un archivo ISO primero.");
        return;
    }

    if (isoCopyManager->copyISO(isoPath)) {
        QMessageBox::information(this, "Copiar ISO", "Función no implementada aún.");
    } else {
        QMessageBox::critical(this, "Error", "Error al copiar el ISO.");
    }
}

void MainWindow::configureBCD()
{
    if (bcdManager->configureBCD()) {
        QMessageBox::information(this, "Configurar BCD", "Función no implementada aún.");
    } else {
        QMessageBox::critical(this, "Error", "Error al configurar BCD.");
    }
}

void MainWindow::openServicesPage()
{
    QDesktopServices::openUrl(QUrl("https://agsoft.co.cr/servicios/")); // Placeholder
}

void MainWindow::updateDiskSpaceInfo()
{
    qint64 availableGB = partitionManager->getAvailableSpaceGB();
    diskSpaceLabel->setText(QString("Espacio disponible en C: %1 GB").arg(availableGB));
}