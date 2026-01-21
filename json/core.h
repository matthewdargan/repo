#ifndef JSON_CORE_H
#define JSON_CORE_H

////////////////////////////////
//~ JSON Types

typedef enum JSON_ValueKind
{
  JSON_ValueKind_Null,
  JSON_ValueKind_Bool,
  JSON_ValueKind_Number,
  JSON_ValueKind_String,
  JSON_ValueKind_Object,
  JSON_ValueKind_Array,
} JSON_ValueKind;

typedef struct JSON_Value JSON_Value;
typedef struct JSON_Member JSON_Member;

struct JSON_Member
{
  JSON_Member *next;
  String8 name;
  JSON_Value *value;
};

struct JSON_Value
{
  JSON_ValueKind kind;
  b32 boolean;         // Bool
  f64 number;          // Number
  String8 string;      // String
  JSON_Member *first;  // Object
  JSON_Member *last;   // Object
  JSON_Value **values; // Array
  u64 count;           // Object, Array
};

////////////////////////////////
//~ JSON API

internal JSON_Value *json_parse(Arena *arena, String8 text);
internal JSON_Value *json_value_from_string(Arena *arena, String8 string);
internal JSON_Value *json_value_from_number(Arena *arena, f64 number);
internal JSON_Value *json_value_from_bool(Arena *arena, b32 value);
internal JSON_Value *json_value_null(Arena *arena);

internal JSON_Value *json_object_alloc(Arena *arena);
internal void json_object_add(Arena *arena, JSON_Value *obj, String8 name, JSON_Value *value);
internal JSON_Value *json_object_get(JSON_Value *obj, String8 name);
internal JSON_Value *json_array_alloc(Arena *arena, u64 capacity);
internal void json_array_add(JSON_Value *arr, JSON_Value *value);

internal String8 json_serialize(Arena *arena, JSON_Value *value);

#endif // JSON_CORE_H
