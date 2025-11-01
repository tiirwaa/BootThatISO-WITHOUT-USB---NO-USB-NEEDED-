#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <windows.h>

const char *const VOLUME_LABEL        = "ISOBOOT";
const char *const EFI_VOLUME_LABEL    = "ISOEFI";
const char *const RAMDISK_BCD_LABEL   = "ISOBOOT_RAM";
const char *const EXTRACTED_BCD_LABEL = "ISOBOOT";

// Log file names
const char *const GENERAL_LOG_FILE          = "general_log.log";
const char *const COPY_ERROR_LOG_FILE       = "copy_error_log.log";
const char *const BCD_CONFIG_LOG_FILE       = "bcd_config_log.log";
const char *const ISO_CONTENT_LOG_FILE      = "iso_content.log";
const char *const ISO_FILE_COPY_LOG_FILE    = "iso_file_copy_log.log";
const char *const ISO_EXTRACT_LOG_FILE      = "iso_extract_log.log";
const char *const DEBUG_DRIVES_EFI_LOG_FILE = "debug_drives_efi.log";
const char *const DEBUG_DRIVES_LOG_FILE     = "debug_drives.log";
const char *const DISKPART_LOG_FILE         = "diskpart_log.log";
const char *const REFORMAT_LOG_FILE         = "reformat_log.log";
const char *const REFORMAT_EXIT_LOG_FILE    = "reformat_exit_log.log";
const char *const CHKDSK_LOG_FILE           = "chkdsk_log.txt";
const char *const CHKDSK_F_LOG_FILE         = "chkdsk_f_log.txt";

// Diskpart error codes
constexpr DWORD DISKPART_DEVICE_IN_USE = 0x80042413;

#endif // CONSTANTS_H