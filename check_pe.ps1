$path = 'Y:\EFI\boot\BOOTX64.EFI'
$fs = [System.IO.File]::OpenRead($path)
$br = New-Object System.IO.BinaryReader($fs)
$fs.Seek(0x3C, 'Begin') | Out-Null
$e_lfanew = $br.ReadInt32()
$fs.Seek(($e_lfanew + 4), 'Begin') | Out-Null
$machine = $br.ReadUInt16()
switch ($machine) {
    0x8664 { "AMD64 / x64 (0x{0:X})" -f $machine }
    0x014c { "x86 / i386 (0x{0:X})" -f $machine }
    0xAA64 { "ARM64 (0x{0:X})" -f $machine }
    default { "Unknown machine: 0x{0:X}" -f $machine }
}
$br.Close()
$fs.Close()