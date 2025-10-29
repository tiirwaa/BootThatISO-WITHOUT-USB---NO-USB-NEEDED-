#ifndef CONSTANTS_H
#define CONSTANTS_H

const char* const VOLUME_LABEL = "ISOBOOT";
const char* const EFI_VOLUME_LABEL = "ISOEFI";
const char* const RAMDISK_BCD_LABEL = "ISOBOOT_RAM";
const char* const EXTRACTED_BCD_LABEL = "ISOBOOT";

// Log file names
const char* const GENERAL_LOG_FILE = "general_log.log";
const char* const COPY_ERROR_LOG_FILE = "copy_error_log.log";
const char* const BCD_CONFIG_LOG_FILE = "bcd_config_log.log";
const char* const ISO_CONTENT_LOG_FILE = "iso_content.log";
const char* const ISO_FILE_COPY_LOG_FILE = "iso_file_copy_log.log";
const char* const ISO_EXTRACT_LOG_FILE = "iso_extract_log.log";

#endif // CONSTANTS_H