#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <windows.h>

const char *const VOLUME_LABEL        = "ISOBOOT";
const char *const EFI_VOLUME_LABEL    = "ISOEFI";
const char *const RAMDISK_BCD_LABEL   = "ISOBOOT_RAM";
const char *const EXTRACTED_BCD_LABEL = "ISOBOOT";

// Partition sizes (MB)
constexpr int REQUIRED_EFI_SIZE_MB = 1024; // 1 GB - standard for all ISOs

// Log file names
const char *const GENERAL_LOG_FILE                  = "general_log.log";
const char *const COPY_ERROR_LOG_FILE               = "copy_error_log.log";
const char *const BCD_CONFIG_LOG_FILE               = "bcd_config_log.log";
const char *const ISO_CONTENT_LOG_FILE              = "iso_content.log";
const char *const ISO_FILE_COPY_LOG_FILE            = "iso_file_copy_log.log";
const char *const ISO_EXTRACT_LOG_FILE              = "iso_extract_log.log";
const char *const DEBUG_DRIVES_EFI_LOG_FILE         = "debug_drives_efi.log";
const char *const DEBUG_DRIVES_LOG_FILE             = "debug_drives.log";
const char *const DISKPART_LOG_FILE                 = "diskpart_log.log";
const char *const EFI_PARTITION_SIZE_LOG_FILE       = "efi_partition_size.log";
const char *const EFI_PARTITION_COUNT_LOG_FILE      = "efi_partition_count.log";
const char *const WINDOWS_EFI_DETECTION_LOG_FILE    = "windows_efi_detection.log";
const char *const REFORMAT_LOG_FILE                 = "reformat_log.log";
const char *const REFORMAT_EXIT_LOG_FILE            = "reformat_exit_log.log";
const char *const CHKDSK_LOG_FILE                   = "chkdsk_log.log";
const char *const CHKDSK_F_LOG_FILE                 = "chkdsk_f_log.log";
const char *const START_PROCESS_LOG_FILE            = "start_process.log";
const char *const UNATTENDED_DEBUG_LOG_FILE         = "unattended_debug.log";
const char *const GENERAL_ALT_LOG_FILE              = "general.log";
const char *const DISKPART_LIST_LOG_FILE            = "diskpart_list_disk.log";
const char *const ISO_TYPE_DETECTION_LOG            = "iso_type_detection.log";
const char *const RECOVER_LOG_FILE                  = "recover_log.log";
const char *const SPACE_VALIDATION_LOG_FILE         = "space_validation.log";
const char *const VERY_EARLY_DEBUG_LOG_FILE         = "very_early_debug.log";
const char *const UNATTENDED_START_LOG_FILE         = "unattended_start.log";
const char *const BCD_CLEANUP_LOG_FILE              = "bcd_cleanup_log.log";
const char *const PARTITION_DETECTOR_ERROR_LOG_FILE = "partition_detector_error.log";
const char *const LIST_VOLUMES_SCRIPT_FILE          = "list_volumes.log";
const char *const VOLUME_LIST_OUTPUT_FILE           = "volume_list.log";
const char *const RESIZE_SCRIPT_FILE                = "resize_script.log";
const char *const ASSIGN_LETTER_SCRIPT_FILE         = "assign_letter.log";
const char *const DELETE_VOLUME_SCRIPT_FILE         = "delete_volume.log";

// Diskpart error codes
constexpr DWORD DISKPART_DEVICE_IN_USE = 0x80042413;

#endif // CONSTANTS_H