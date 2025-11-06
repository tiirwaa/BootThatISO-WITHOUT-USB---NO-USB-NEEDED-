@echo off
REM Script to regenerate embedded GRUB EFI chainloader
REM Run this when updating chainloader\grubx64.efi

echo Regenerating embedded GRUB EFI chainloader...

REM Convert binary to C++ headers
python tools\binary_to_header.py chainloader\grubx64.efi include\grubx64_efi.h grubx64_efi_data

REM Rebuild the project
call compilar.bat

echo Embedded chainloader regeneration complete!
echo New executable size:
powershell "Get-Item build\Release\BootThatISO!.exe | Select-Object -ExpandProperty Length"