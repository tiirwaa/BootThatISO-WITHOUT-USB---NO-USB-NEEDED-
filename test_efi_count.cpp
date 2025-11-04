#include <iostream>
#include "src/models/VolumeDetector.h"

int main() {
    VolumeDetector detector(nullptr);
    int count = detector.countEfiPartitions();
    
    std::cout << "\n========================================\n";
    std::cout << "Test de conteo de particiones ISOEFI\n";
    std::cout << "========================================\n";
    std::cout << "Particiones ISOEFI encontradas: " << count << "\n";
    
    if (count == 1) {
        std::cout << "✓ CORRECTO: Solo se detectó 1 partición\n";
    } else {
        std::cout << "✗ ERROR: Se detectaron " << count << " particiones (debería ser 1)\n";
    }
    std::cout << "========================================\n";
    std::cout << "\nRevisar el log en: logs\\efi_partition_count.log\n\n";
    
    return 0;
}
