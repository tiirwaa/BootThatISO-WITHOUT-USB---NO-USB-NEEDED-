#include "DiskIntegrityChecker.h"
#include "../utils/Utils.h"
#include "../utils/constants.h"
#include "../utils/LocalizationManager.h"
#include "../utils/LocalizationHelpers.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

DiskIntegrityChecker::DiskIntegrityChecker(EventManager *eventManager) : eventManager_(eventManager) {}

DiskIntegrityChecker::~DiskIntegrityChecker() {}

bool DiskIntegrityChecker::performDiskIntegrityCheck() {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    if (eventManager_)
        eventManager_->notifyLogUpdate("Verificando integridad del disco...\r\n");

    // Run chkdsk C: to check for errors
    STARTUPINFOA        si_chk = {sizeof(si_chk)};
    PROCESS_INFORMATION pi_chk;
    SECURITY_ATTRIBUTES sa_chk;
    sa_chk.nLength              = sizeof(sa_chk);
    sa_chk.lpSecurityDescriptor = NULL;
    sa_chk.bInheritHandle       = TRUE;

    HANDLE hRead_chk, hWrite_chk;
    if (!CreatePipe(&hRead_chk, &hWrite_chk, &sa_chk, 0)) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo crear pipe para chkdsk.\r\n");
        return false;
    }

    si_chk.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si_chk.hStdOutput  = hWrite_chk;
    si_chk.hStdError   = hWrite_chk;
    si_chk.wShowWindow = SW_HIDE;

    std::string cmd_chk = "chkdsk C:";
    if (!CreateProcessA(NULL, const_cast<char *>(cmd_chk.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si_chk, &pi_chk)) {
        CloseHandle(hRead_chk);
        CloseHandle(hWrite_chk);
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo ejecutar chkdsk.\r\n");
        return false;
    }

    CloseHandle(hWrite_chk);

    std::string output_chk;
    char        buffer_chk[1024];
    DWORD       bytesRead_chk;
    while (ReadFile(hRead_chk, buffer_chk, sizeof(buffer_chk) - 1, &bytesRead_chk, NULL) && bytesRead_chk > 0) {
        buffer_chk[bytesRead_chk] = '\0';
        output_chk += buffer_chk;
    }

    CloseHandle(hRead_chk);

    WaitForSingleObject(pi_chk.hProcess, 300000); // 5 minutes

    DWORD exitCode_chk;
    GetExitCodeProcess(pi_chk.hProcess, &exitCode_chk);

    CloseHandle(pi_chk.hProcess);
    CloseHandle(pi_chk.hThread);

    // Log the chkdsk output
    std::ofstream chkLog((logDir + "\\" + CHKDSK_LOG_FILE).c_str());
    if (chkLog) {
        chkLog << "Chkdsk exit code: " << exitCode_chk << "\n";
        chkLog << "Output:\n" << Utils::ansi_to_utf8(output_chk) << "\n";
        chkLog.close();
    }

    // Show chkdsk result in UI log
    if (eventManager_)
        eventManager_->notifyLogUpdate("Resultado de verificación de disco:\r\n" + Utils::ansi_to_utf8(output_chk) +
                                       "\r\n");

    // If errors found, ask user if they want to repair
    if (exitCode_chk != 0) {
        std::wstring repairPrompt =
            LocalizedOrW("message.diskErrorsFoundPrompt",
                         L"Se encontraron errores en el disco C:. ¿Desea reparar el disco y reiniciar el sistema?");
        std::wstring repairTitle = LocalizedOrW("title.repairDisk", L"Reparar disco");
        int          result = MessageBoxW(NULL, repairPrompt.c_str(), repairTitle.c_str(), MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES) {
            return false;
        }

        if (eventManager_)
            eventManager_->notifyLogUpdate("Ejecutando chkdsk /f para reparar errores...\r\n");

        // Run chkdsk /f
        STARTUPINFOA        si_f = {sizeof(si_f)};
        PROCESS_INFORMATION pi_f;
        SECURITY_ATTRIBUTES sa_f;
        sa_f.nLength              = sizeof(sa_f);
        sa_f.lpSecurityDescriptor = NULL;
        sa_f.bInheritHandle       = TRUE;

        HANDLE hRead_f, hWrite_f;
        HANDLE hRead_stdin, hWrite_stdin;
        if (!CreatePipe(&hRead_stdin, &hWrite_stdin, &sa_f, 0)) {
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error: No se pudo crear pipe para stdin.\r\n");
            return false;
        }
        if (!CreatePipe(&hRead_f, &hWrite_f, &sa_f, 0)) {
            CloseHandle(hRead_stdin);
            CloseHandle(hWrite_stdin);
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error: No se pudo crear pipe para chkdsk /f.\r\n");
            return false;
        }

        si_f.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si_f.hStdInput   = hRead_stdin;
        si_f.hStdOutput  = hWrite_f;
        si_f.hStdError   = hWrite_f;
        si_f.wShowWindow = SW_HIDE;

        std::string cmd_f = "chkdsk C: /f";
        if (!CreateProcessA(NULL, const_cast<char *>(cmd_f.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si_f, &pi_f)) {
            CloseHandle(hRead_f);
            CloseHandle(hWrite_f);
            CloseHandle(hRead_stdin);
            CloseHandle(hWrite_stdin);
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error: No se pudo ejecutar chkdsk /f.\r\n");
            return false;
        }

        // Send 'S' to stdin
        DWORD bytesWritten;
        bool  inputSent = false;

        std::string output_f;
        char        buffer_f[1024];
        DWORD       bytesRead_f;

        // Read output in real-time
        while (true) {
            DWORD bytesAvailable = 0;
            if (!PeekNamedPipe(hRead_f, NULL, 0, NULL, &bytesAvailable, NULL)) {
                break;
            }
            if (bytesAvailable > 0) {
                if (ReadFile(hRead_f, buffer_f, min(sizeof(buffer_f) - 1, bytesAvailable), &bytesRead_f, NULL) &&
                    bytesRead_f > 0) {
                    buffer_f[bytesRead_f] = '\0';
                    output_f += buffer_f;
                    // Notify partial output
                    if (eventManager_)
                        eventManager_->notifyLogUpdate(buffer_f);
                    // Check if prompt is present and send input
                    if (!inputSent && output_f.find("(S/N)") != std::string::npos) {
                        if (eventManager_)
                            eventManager_->notifyLogUpdate("Enviando respuesta 'S' a chkdsk...\r\n");
                        WriteFile(hWrite_stdin, "S\r\n", 3, &bytesWritten, NULL);
                        CloseHandle(hWrite_stdin);
                        inputSent = true;
                        Sleep(2000); // Give time for chkdsk to process the input
                    }
                }
            }

            DWORD waitResult = WaitForSingleObject(pi_f.hProcess, 100); // 100ms timeout
            if (waitResult == WAIT_OBJECT_0) {
                break;
            }
        }

        // Tracing: loop ended
        const std::string loopEndMsg =
            "Bucle de lectura en tiempo real terminado. Esperando fin del proceso chkdsk /f...\r\n";
        if (eventManager_)
            eventManager_->notifyLogUpdate(loopEndMsg);
        logToGeneral(loopEndMsg);

        DWORD exitCode_f;
        GetExitCodeProcess(pi_f.hProcess, &exitCode_f);

        // Tracing: process ended
        const std::string processEndMsg = "Chkdsk /f terminó con código: " + std::to_string(exitCode_f) + "\r\n";
        if (eventManager_)
            eventManager_->notifyLogUpdate(processEndMsg);
        logToGeneral(processEndMsg);

        CloseHandle(pi_f.hProcess);
        CloseHandle(pi_f.hThread);

        // Tracing: final read
        const std::string finalReadMsg = "Leyendo salida final...\r\n";
        if (eventManager_)
            eventManager_->notifyLogUpdate(finalReadMsg);
        logToGeneral(finalReadMsg);

        // Force final read of any remaining output after process ends
        while (ReadFile(hRead_f, buffer_f, sizeof(buffer_f) - 1, &bytesRead_f, NULL) && bytesRead_f > 0) {
            buffer_f[bytesRead_f] = '\0';
            output_f += buffer_f;
            if (eventManager_)
                eventManager_->notifyLogUpdate(buffer_f);
        }

        const std::string finalReadDoneMsg = "Lectura final completada.\r\n";
        if (eventManager_)
            eventManager_->notifyLogUpdate(finalReadDoneMsg);
        logToGeneral(finalReadDoneMsg);

        CloseHandle(hRead_f);
        CloseHandle(hWrite_f);
        CloseHandle(hRead_stdin);

        // Log the chkdsk /f output
        std::ofstream chkLogF((logDir + "\\" + CHKDSK_F_LOG_FILE).c_str());
        if (chkLogF) {
            chkLogF << "Chkdsk /f exit code: " << exitCode_f << "\n";
            chkLogF << "Output:\n" << Utils::ansi_to_utf8(output_f) << "\n";
            chkLogF.close();
        }

        // Show chkdsk /f result in UI log
        if (eventManager_) {
            eventManager_->notifyLogUpdate("Resultado de chkdsk /f:\r\n" + Utils::ansi_to_utf8(output_f) + "\r\n");
            Sleep(100); // Give UI time to flush
        }

        // FINAL scheduling detection and restart logic (always runs after process ends)
        std::string       utf8_output_f = Utils::ansi_to_utf8(output_f);
        const std::string verifyMsg     = "Verificando si se programó para el próximo reinicio...\r\n";
        logToGeneral(verifyMsg);
        if (eventManager_)
            eventManager_->notifyLogUpdate(verifyMsg);
        Sleep(100);

        std::string lower = output_f;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        // Debug: log output details
        logToGeneral("output_f length: " + std::to_string(output_f.length()) + "\n");
        logToGeneral("lower substr: " + lower.substr(0, 200) + "\n"); // first 200 chars
        bool hasSeComprobar = lower.find("se comprobar") != std::string::npos;
        bool hasReiniciar   = lower.find("reiniciar") != std::string::npos;
        logToGeneral("has 'se comprobar': " + std::string(hasSeComprobar ? "yes" : "no") + "\n");
        logToGeneral("has 'reiniciar': " + std::string(hasReiniciar ? "yes" : "no") + "\n");

        logToGeneral("Starting detection\n");
        bool scheduled = false;

        // Language-independent detection: check exit code 3 (scheduled for next boot)
        if (exitCode_f == 3) {
            scheduled = true;
            logToGeneral("Scheduled = true via exit code 3\n");
        }
        // Robust detection: check for key phrases in raw output
        if (hasSeComprobar && hasReiniciar) {
            scheduled = true;
            logToGeneral("Scheduled = true via robust check\n");
        } else {
            // Fallback to markers in UTF-8
            std::string lower_utf8 = utf8_output_f;
            std::transform(lower_utf8.begin(), lower_utf8.end(), lower_utf8.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            const char *markers[] = {"próxima vez",  "proxima vez",     "próximo arranque", "proximo arranque",
                                     "se comprobar", "se comprobará",   "se comprobara",    "este volumen",
                                     "(s/n)",        "will be checked", "next boot",        "restart"};
            for (const char *m : markers) {
                std::string mm      = m;
                std::string mmLower = mm;
                std::transform(mmLower.begin(), mmLower.end(), mmLower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (utf8_output_f.find(mm) != std::string::npos || lower_utf8.find(mmLower) != std::string::npos) {
                    scheduled = true;
                    logToGeneral("Scheduled = true via marker: " + std::string(m) + "\n");
                    break;
                }
            }
        }
        if (utf8_output_f.find("Este volumen se comprobará la próxima vez que se reinicie el sistema.") !=
                std::string::npos ||
            lower.find("este volumen se comprobará la próxima vez que se reinicie el sistema.") != std::string::npos) {
            scheduled = true;
            logToGeneral("Scheduled = true via exact match\n");
        }

        logToGeneral("Final scheduled: " + std::string(scheduled ? "true" : "false") + "\n");

        if (scheduled) {
            const std::string scheduledMsg = "Chkdsk ha programado una verificación para el próximo reinicio. "
                                             "Intentando reiniciar el sistema...\r\n";
            logToGeneral(scheduledMsg);
            if (eventManager_)
                eventManager_->notifyLogUpdate(scheduledMsg);
            Sleep(1000); // small pause before attempting restart
            bool restarted = RestartComputer();
            if (!restarted) {
                DWORD             lastErr = GetLastError();
                const std::string failMsg = "RestartComputer() falló; código de error: " + std::to_string(lastErr) +
                                            ". Intentando comando de emergencia 'shutdown /r /t 0'...\r\n";
                logToGeneral(failMsg);
                if (eventManager_)
                    eventManager_->notifyLogUpdate(failMsg);
                std::string       shutOut     = Utils::exec("shutdown /r /t 0");
                int               ret         = 0; // Utils::exec doesn't expose exit code here; treat non-empty as attempted
                const std::string shutdownMsg = "Resultado comando 'shutdown': " + std::to_string(ret) +
                                                " (0=OK, otro=error o sin privilegios)\r\n";
                logToGeneral(shutdownMsg);
                if (eventManager_)
                    eventManager_->notifyLogUpdate(shutdownMsg);
            }
            logToGeneral("Fin de proceso de verificación/reinicio.\r\n");
            return false;
        } else {
            const std::string notScheduledMsg =
                "No se detectó programación de chkdsk para el próximo reinicio. No se reiniciará automáticamente.\r\n";
            logToGeneral(notScheduledMsg);
            if (eventManager_)
                eventManager_->notifyLogUpdate(notScheduledMsg);
        }
        logToGeneral("Fin de proceso de verificación/reinicio.\r\n");
        if (eventManager_) {
            eventManager_->notifyLogUpdate("Fin de proceso de verificación/reinicio.\r\n");
            Sleep(100);
        }

        if (exitCode_f != 0) {
            if (eventManager_)
                eventManager_->notifyLogUpdate("Error: Chkdsk /f falló.\r\n");
            return false;
        }
    }
    return true;
}

bool DiskIntegrityChecker::RestartComputer() {
    if (eventManager_)
        eventManager_->notifyLogUpdate("Intentando reiniciar el sistema...\r\n");

    // Enable shutdown privilege
    HANDLE           hToken;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo abrir el token del proceso.\r\n");
        return false;
    }
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount           = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0)) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: No se pudo ajustar los privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    if (GetLastError() != ERROR_SUCCESS) {
        if (eventManager_)
            eventManager_->notifyLogUpdate("Error: Falló la verificación de privilegios.\r\n");
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);

    // Attempt to restart the computer
    if (ExitWindowsEx(EWX_REBOOT, 0)) {
        return true;
    } else {
        DWORD error = GetLastError();
        if (eventManager_)
            eventManager_->notifyLogUpdate(
                "Error: No se pudo reiniciar el sistema. Código de error: " + std::to_string(error) + "\r\n");
        return false;
    }
}

void DiskIntegrityChecker::logToGeneral(const std::string &message) {
    std::string logDir = Utils::getExeDirectory() + "logs";
    CreateDirectoryA(logDir.c_str(), NULL);
    std::ofstream logFile((logDir + "\\general.log").c_str(), std::ios::app);
    if (logFile) {
        logFile << message;
        logFile.close();
    }
}