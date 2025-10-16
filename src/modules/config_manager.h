#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "config_schema.h"
#include <stdbool.h>

/**
 * Initialize new configuration manager with unified schema
 * @return true on success
 */
bool config_manager_init(void);

/**
 * Load configuration from NVS
 * @return true on success
 */
bool config_manager_load(void);

/**
 * Save configuration to NVS
 * @return true on success
 */
bool config_manager_save(void);

/**
 * Reset configuration to factory defaults
 * @return true on success
 */
bool config_manager_reset_to_factory(void);

/**
 * Get complete unified configuration
 * @param config Output configuration structure
 * @return true on success
 */
bool config_manager_get_config(unified_config_t *config);

/**
 * Set complete unified configuration
 * @param config Configuration structure to set
 * @return true on success
 */
bool config_manager_set_config(const unified_config_t *config);

/**
 * Get single field value
 * @param field_id Field identifier
 * @param buffer Buffer to store value
 * @param buffer_size Size of buffer
 * @return true on success
 */
bool config_manager_get_field(config_field_id_t field_id,
                              char *buffer,
                              size_t buffer_size);

/**
 * Set single field value
 * @param field_id Field identifier
 * @param value Value to set
 * @param result Validation result (optional)
 * @return true on success
 */
bool config_manager_set_field(config_field_id_t field_id,
                              const char *value,
                              config_validation_result_t *result);

/**
 * Get fields by category
 * @param category Category name (e.g., "wifi", "audio", "buffer")
 * @param output JSON output buffer
 * @param buffer_size Size of output buffer
 * @return true on success
 */
bool config_manager_get_category(const char *category,
                                 char *output,
                                 size_t buffer_size);

/**
 * Set fields by category
 * @param category Category name
 * @param json_input JSON input with field values
 * @param results Array to store validation results
 * @param max_results Maximum number of results
 * @return Number of validation issues (0 = success)
 */
size_t config_manager_set_category(const char *category,
                                   const char *json_input,
                                   config_validation_result_t *results,
                                   size_t max_results);

/**
 * Validate current configuration
 * @param results Array to store validation results
 * @param max_results Maximum number of results
 * @return Number of validation issues (0 = valid)
 */
size_t config_manager_validate(config_validation_result_t *results,
                               size_t max_results);

/**
 * Export configuration to JSON
 * @param json_output Buffer to store JSON
 * @param buffer_size Size of buffer
 * @return true on success
 */
bool config_manager_export_json(char *json_output, size_t buffer_size);

/**
 * Import configuration from JSON
 * @param json_input JSON input
 * @param overwrite Whether to overwrite existing values
 * @param results Array to store validation results
 * @param max_results Maximum number of results
 * @return Number of validation issues (0 = success)
 */
size_t config_manager_import_json(const char *json_input,
                                  bool overwrite,
                                  config_validation_result_t *results,
                                  size_t max_results);

/**
 * Get configuration version
 * @return Current version
 */
uint8_t config_manager_get_version(void);

/**
 * Check if first boot
 * @return true if first boot
 */
bool config_manager_is_first_boot(void);

/**
 * Get field metadata
 * @param field_id Field identifier
 * @return Field metadata
 */
const config_field_meta_t *config_manager_get_field_meta(config_field_id_t field_id);

/**
 * Get all categories
 * @param categories Array to store category names
 * @param max_categories Maximum number of categories
 * @return Number of categories
 */
size_t config_manager_get_categories(char **categories, size_t max_categories);

/**
 * Check if restart is required for changes
 * @param field_id Field identifier
 * @return true if restart required
 */
bool config_manager_restart_required(config_field_id_t field_id);

/**
 * Get field default value
 * @param field_id Field identifier
 * @param buffer Buffer to store default
 * @param buffer_size Size of buffer
 * @return true on success
 */
bool config_manager_get_field_default(config_field_id_t field_id,
                                      char *buffer,
                                      size_t buffer_size);

/**
 * Reset field to default value
 * @param field_id Field identifier
 * @return true on success
 */
bool config_manager_reset_field(config_field_id_t field_id);

/**
 * Check if configuration has unsaved changes
 * @return true if there are unsaved changes
 */
bool config_manager_has_unsaved_changes(void);

/**
 * Mark configuration as saved
 */
void config_manager_mark_saved(void);

/**
 * Deinitialize configuration manager
 */
void config_manager_deinit(void);

#endif // CONFIG_MANAGER_H
