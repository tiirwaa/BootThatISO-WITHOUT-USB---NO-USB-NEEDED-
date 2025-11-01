#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

int main() {
    std::string inputFile = "HBCD_PE.ini";
    std::string outputFile = "HBCD_PE_modified.ini";

    std::ifstream iniFile(inputFile);
    if (!iniFile.is_open()) {
        std::cerr << "Error: No se pudo abrir el archivo " << inputFile << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << iniFile.rdbuf();
    std::string iniContent = buffer.str();
    iniFile.close();

    std::cout << "Contenido original:" << std::endl;
    std::cout << iniContent << std::endl;
    std::cout << "------------------------" << std::endl;

    // Reemplazar Y: por X:
    size_t pos = 0;
    while ((pos = iniContent.find("Y:\\", pos)) != std::string::npos) {
        iniContent.replace(pos, 3, "X:\\");
        pos += 3;
    }

    std::cout << "Contenido modificado:" << std::endl;
    std::cout << iniContent << std::endl;

    std::ofstream outIniFile(outputFile);
    if (outIniFile.is_open()) {
        outIniFile << iniContent;
        outIniFile.close();
        std::cout << "Archivo modificado guardado como " << outputFile << std::endl;
    } else {
        std::cerr << "Error: No se pudo guardar el archivo " << outputFile << std::endl;
        return 1;
    }

    return 0;
}