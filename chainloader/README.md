grub-mkimage \
  -O x86_64-efi \
  -o grubx64.efi \
  -p /EFI/grub \
  part_gpt part_msdos fat ntfs exfat ext2 \
  normal linux loopback chain configfile search echo