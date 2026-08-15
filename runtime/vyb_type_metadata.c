// SPDX-License-Identifier: Apache-2.0

// Vyb Type Metadata Runtime Implementation
#include "vyb_type_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Heap-string registry hooks (defined in vyb_runtime.c). Registering a JSON
// String field lets the generated code's release path reclaim the buffer.
extern void __vyb_string_register(void* p);

// Global type registry (simple linear search for now)
#define MAX_TYPES 256
static VybTypeMetadata* g_type_registry[MAX_TYPES];
static size_t g_num_types = 0;

// Register a type in the global registry
void __vyb_register_type(VybTypeMetadata* metadata) {
    if (g_num_types >= MAX_TYPES) {
        fprintf(stderr, "Error: Type registry full\n");
        return;
    }
    g_type_registry[g_num_types++] = metadata;
}

// Lookup type metadata by name
VybTypeMetadata* __vyb_lookup_type(const char* type_name) {
    for (size_t i = 0; i < g_num_types; i++) {
        if (strcmp(g_type_registry[i]->type_name, type_name) == 0) {
            return g_type_registry[i];
        }
    }
    return NULL;
}

// Helper: Serialize a primitive field to JSON
static void serialize_primitive_field(char* buffer, size_t* pos, void* field_ptr,
                                     const char* type_name, bool is_last) {
    if (strcmp(type_name, "Int") == 0) {
        int64_t val = *(int64_t*)field_ptr;
        *pos += sprintf(buffer + *pos, "%ld", (long)val);
    } else if (strcmp(type_name, "Float") == 0) {
        double val = *(double*)field_ptr;
        *pos += sprintf(buffer + *pos, "%g", val);
    } else if (strcmp(type_name, "Bool") == 0) {
        bool val = *(bool*)field_ptr;
        *pos += sprintf(buffer + *pos, "%s", val ? "true" : "false");
    } else if (strcmp(type_name, "String") == 0) {
        // String is stored as {char* data, int64_t length} struct
        typedef struct { char* data; int64_t length; } VybString;
        VybString* str = (VybString*)field_ptr;
        *pos += sprintf(buffer + *pos, "\"");
        if (str->data && str->length > 0) {
            // Use length for safety
            for (int64_t i = 0; i < str->length && str->data[i]; i++) {
                // Escape special JSON characters
                char c = str->data[i];
                if (c == '"') {
                    *pos += sprintf(buffer + *pos, "\\\"");
                } else if (c == '\\') {
                    *pos += sprintf(buffer + *pos, "\\\\");
                } else if (c == '\n') {
                    *pos += sprintf(buffer + *pos, "\\n");
                } else if (c == '\t') {
                    *pos += sprintf(buffer + *pos, "\\t");
                } else {
                    buffer[(*pos)++] = c;
                }
            }
        }
        *pos += sprintf(buffer + *pos, "\"");
    } else {
        *pos += sprintf(buffer + *pos, "null");
    }

    *pos += sprintf(buffer + *pos, "%s", is_last ? "" : ", ");
}

// Serialize a complex type to JSON using its metadata
char* __vyb_complex_to_json_with_metadata(void* instance, VybTypeMetadata* metadata) {
    if (!instance || !metadata) {
        return strdup("null");
    }

    // Allocate buffer (TODO: dynamic sizing)
    char* buffer = (char*)malloc(4096);
    size_t pos = 0;

    // Start JSON object
    pos += sprintf(buffer + pos, "{");

    // Serialize each field
    for (size_t i = 0; i < metadata->num_fields; i++) {
        VybFieldMetadata* field = &metadata->fields[i];
        void* field_ptr = (char*)instance + field->offset;

        fprintf(stderr, "DEBUG JSON: Field '%s' at offset %zu, type '%s', is_primitive=%d\n",
                field->name, field->offset, field->type_name, field->is_primitive);

        // Field name
        pos += sprintf(buffer + pos, "\"%s\": ", field->name);

        if (field->is_primitive) {
            // Primitive types
            serialize_primitive_field(buffer, &pos, field_ptr, field->type_name,
                                     i == metadata->num_fields - 1);
        } else if (field->is_vec) {
            // Vec types (TODO: implement)
            pos += sprintf(buffer + pos, "[]%s", i == metadata->num_fields - 1 ? "" : ", ");
        } else {
            // Nested complex type (recursive)
            VybTypeMetadata* nested_meta = __vyb_lookup_type(field->type_name);
            if (nested_meta) {
                char* nested_json = __vyb_complex_to_json_with_metadata(field_ptr, nested_meta);
                pos += sprintf(buffer + pos, "%s%s", nested_json,
                             i == metadata->num_fields - 1 ? "" : ", ");
                free(nested_json);
            } else {
                pos += sprintf(buffer + pos, "null%s", i == metadata->num_fields - 1 ? "" : ", ");
            }
        }
    }

    // End JSON object
    pos += sprintf(buffer + pos, "}");
    buffer[pos] = '\0';
    return buffer;
}

// Simple JSON parser helper (very basic - production would use a real JSON library)
static const char* skip_whitespace(const char* json) {
    while (*json == ' ' || *json == '\t' || *json == '\n' || *json == '\r') json++;
    return json;
}

static const char* parse_string_value(const char* json, char* out, size_t max_len) {
    json = skip_whitespace(json);
    if (*json != '"') return NULL;
    json++; // Skip opening quote

    size_t i = 0;
    while (*json && *json != '"' && i < max_len - 1) {
        out[i++] = *json++;
    }
    out[i] = '\0';

    if (*json == '"') json++; // Skip closing quote
    return json;
}

static const char* parse_number_value(const char* json, int64_t* out) {
    json = skip_whitespace(json);
    // Numbers in JSON are NOT quoted
    *out = strtoll(json, (char**)&json, 10);
    return json;
}

static const char* parse_float_value(const char* json, double* out) {
    json = skip_whitespace(json);
    // Numbers in JSON are NOT quoted
    *out = strtod(json, (char**)&json);
    return json;
}

static const char* parse_bool_value(const char* json, bool* out) {
    json = skip_whitespace(json);
    // Booleans in JSON are NOT quoted
    if (strncmp(json, "true", 4) == 0) {
        *out = true;
        json += 4;
    } else if (strncmp(json, "false", 5) == 0) {
        *out = false;
        json += 5;
    }
    return json;
}

// Deserialize JSON to a complex type using metadata
void* __vyb_complex_from_json_with_metadata(const char* json_str, VybTypeMetadata* metadata) {
    if (!json_str || !metadata) {
        return NULL;
    }

    // Allocate instance
    void* instance = calloc(1, metadata->struct_size);
    if (!instance) return NULL;

    // Parse JSON (very simplified parser - expects {"field": "value", ...})
    const char* p = skip_whitespace(json_str);
    if (*p != '{') {
        free(instance);
        return NULL;
    }
    p++; // Skip '{'

    // Parse fields
    while (*p && *p != '}') {
        p = skip_whitespace(p);
        if (*p == '}') break;

        // Parse field name
        if (*p != '"') break;
        p++; // Skip opening quote

        char field_name[256];
        size_t i = 0;
        while (*p && *p != '"' && i < 255) {
            field_name[i++] = *p++;
        }
        field_name[i] = '\0';

        if (*p == '"') p++; // Skip closing quote
        p = skip_whitespace(p);
        if (*p != ':') break;
        p++; // Skip ':'

        // Find field in metadata
        VybFieldMetadata* field = NULL;
        for (size_t j = 0; j < metadata->num_fields; j++) {
            if (strcmp(metadata->fields[j].name, field_name) == 0) {
                field = &metadata->fields[j];
                break;
            }
        }

        if (field) {
            void* field_ptr = (char*)instance + field->offset;

            if (field->is_primitive) {
                if (strcmp(field->type_name, "Int") == 0) {
                    int64_t val;
                    p = parse_number_value(p, &val);
                    *(int64_t*)field_ptr = val;
                } else if (strcmp(field->type_name, "Float") == 0) {
                    double val;
                    p = parse_float_value(p, &val);
                    *(double*)field_ptr = val;
                } else if (strcmp(field->type_name, "Bool") == 0) {
                    bool val;
                    p = parse_bool_value(p, &val);
                    *(bool*)field_ptr = val;
                } else if (strcmp(field->type_name, "String") == 0) {
                    char str_val[1024];
                    p = parse_string_value(p, str_val, sizeof(str_val));
                    // String is {char* data, int64_t length} struct
                    typedef struct { char* data; int64_t length; } VybString;
                    VybString* vyb_str = (VybString*)field_ptr;
                    vyb_str->data = strdup(str_val);
                    vyb_str->length = strlen(str_val);
                    __vyb_string_register(vyb_str->data);
                }
            }
            // TODO: Handle nested structs and Vec
        }

        // Skip to next field or end
        p = skip_whitespace(p);
        if (*p == ',') p++;
    }

    return instance;
}
