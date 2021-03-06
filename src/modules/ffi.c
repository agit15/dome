#if DOME_OPT_FFI

typedef struct {
  void* handle;
  char name[];
} LIBRARY_HANDLE;

typedef struct {
  ffi_type typeData;
  size_t elementCount;
  ffi_type* elements[];
} STRUCT_TYPE;

typedef struct {
  STRUCT_TYPE* dataType;
  uint8_t* start;
  char blob[];
} STRUCT;

internal void
LIBRARY_HANDLE_allocate(WrenVM* vm) {
  char* libraryName = wrenGetSlotString(vm, 1);
  void* handle = SDL_LoadObject(libraryName);
  if (handle == NULL) {
    wrenSetSlotString(vm, 1, SDL_GetError());
    wrenAbortFiber(vm, 1);
    return;
  }
  LIBRARY_HANDLE* library = (LIBRARY_HANDLE*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(LIBRARY_HANDLE) + sizeof(char) * strlen(libraryName) + 1);

  library->handle = handle;
  strcpy(library->name, libraryName);

  // TODO: Register library in library map
  // and functionmap
}

internal void
LIBRARY_HANDLE_finalize(void* ptr) {
  LIBRARY_HANDLE* handle = ptr;
  printf("SDL Unload Library: %s", handle->name);
  SDL_UnloadObject(handle);
}


typedef struct {
  void* methodPtr;
  ffi_cif cif;
  ffi_type* argTypes[];
} FUNCTION;

ffi_type* toFFIType(char* name) {
  if (STRINGS_EQUAL(name, "void")) {
    return &ffi_type_void;
  } else if (STRINGS_EQUAL(name, "long")) {
    return &ffi_type_slong;
  } else if (STRINGS_EQUAL(name, "unsigned long")) {
    return &ffi_type_ulong;
  } else if (STRINGS_EQUAL(name, "short")) {
    return &ffi_type_sshort;
  } else if (STRINGS_EQUAL(name, "unsigned short")) {
    return &ffi_type_ushort;
  } else if (STRINGS_EQUAL(name, "char")) {
    return &ffi_type_schar;
  } else if (STRINGS_EQUAL(name, "unsigned char")) {
    return &ffi_type_uchar;
  } else if (STRINGS_EQUAL(name, "int8_t")) {
    return &ffi_type_sint8;
  } else if (STRINGS_EQUAL(name, "uint8_t")) {
    return &ffi_type_uint8;
  } else if (STRINGS_EQUAL(name, "int16_t")) {
    return &ffi_type_sint16;
  } else if (STRINGS_EQUAL(name, "uint16_t")) {
    return &ffi_type_uint16;
  } else if (STRINGS_EQUAL(name, "int32_t")) {
    return &ffi_type_sint32;
  } else if (STRINGS_EQUAL(name, "uint32_t")) {
    return &ffi_type_uint32;
  } else if (STRINGS_EQUAL(name, "int64_t")) {
    return &ffi_type_sint64;
  } else if (STRINGS_EQUAL(name, "uint64_t")) {
    return &ffi_type_uint64;
  } else if (STRINGS_EQUAL(name, "unsigned int")) {
    return &ffi_type_uint;
  } else if (STRINGS_EQUAL(name, "int")) {
    return &ffi_type_sint;
  } else if (STRINGS_EQUAL(name, "float")) {
    return &ffi_type_float;
  } else if (STRINGS_EQUAL(name, "double")) {
    return &ffi_type_double;
  } else if (STRINGS_EQUAL(name, "pointer")) {
    return &ffi_type_pointer;
    // TODO: handle other types

  } else {
    // TODO: handle structs
    return &ffi_type_pointer;
  }
}

internal void
FUNCTION_allocate(WrenVM* vm) {
  LIBRARY_HANDLE* library = wrenGetSlotForeign(vm, 1);
  char* fnName = wrenGetSlotString(vm, 2);
  printf("%s\n", fnName);
  char* returnType = wrenGetSlotString(vm, 3);
  size_t argCount = wrenGetListCount(vm, 4);
  // TODO: Variadic functions
  FUNCTION* function = wrenSetSlotNewForeign(vm, 0, 0, sizeof(FUNCTION) + sizeof(ffi_type*) * argCount);
  function->methodPtr = SDL_LoadFunction(library->handle, fnName);
  if (function->methodPtr == NULL) {
    wrenSetSlotString(vm, 1, "Could not bind to function");
    wrenAbortFiber(vm, 1);
  }

  ffi_type* retType = toFFIType(returnType);

  ffi_type** argTypes = function->argTypes;
  for (size_t i = 0; i < argCount; i++) {
    // Move element i from List to slot 3
    wrenGetListElement(vm, 4, i, 3);
    if (wrenGetSlotType(vm, 3) == WREN_TYPE_STRING) {
      char* typeName = wrenGetSlotString(vm, 3);
      printf("%s\n", typeName);
      argTypes[i] = toFFIType(typeName);
    } else if (wrenGetSlotType(vm, 3) == WREN_TYPE_FOREIGN) {
      STRUCT_TYPE* data = (STRUCT_TYPE*)wrenGetSlotForeign(vm, 3);
      argTypes[i] = &(data->typeData);
    } else {
      VM_ABORT(vm, "Invalid argument type");
      return;
    }
  }

  ffi_status result = ffi_prep_cif(&function->cif, FFI_DEFAULT_ABI, argCount, retType, argTypes);

  if (result == FFI_BAD_TYPEDEF) {
    wrenSetSlotString(vm, 1, "Invalid FFI Typedef");
    wrenAbortFiber(vm, 1);
  } else if (result == FFI_BAD_ABI) {
    wrenSetSlotString(vm, 1, "Invalid FFI ABI");
    wrenAbortFiber(vm, 1);
  }
}

internal void
FUNCTION_finalize(void* function) {
  // free(function);
}

internal void
FUNCTION_call(WrenVM* vm) {
  wrenEnsureSlots(vm, 3);
  FUNCTION* function = wrenGetSlotForeign(vm, 0);
  unsigned int argCount = wrenGetListCount(vm, 1);
  void* args[argCount];
  ffi_type** argTypes = function->cif.arg_types;

  if (argCount != function->cif.nargs) {
    wrenSetSlotString(vm, 1, "FFI: Argument mismatch");
    wrenAbortFiber(vm, 1);
    return;
  }

  for (unsigned int i = 0; i < argCount; i++) {
    // Move element i from List to slot 2
    wrenGetListElement(vm, 1, i, 2);
    ffi_type* ffiType = argTypes[i];
    args[i] = alloca(sizeof(ffiType->size));
    switch (ffiType->type) {
      case FFI_TYPE_FLOAT: {
        if (wrenGetSlotType(vm, 2) != WREN_TYPE_NUM) {
          goto fail_cast;
        }
        float* value = args[i];
        *value = wrenGetSlotDouble(vm, 2);
      } break;
      case FFI_TYPE_DOUBLE: {
        if (wrenGetSlotType(vm, 2) != WREN_TYPE_NUM) {
          goto fail_cast;
        }
        double* value = args[i];
        *value = wrenGetSlotDouble(vm, 2);
      } break;
      case FFI_TYPE_INT:
      case FFI_TYPE_SINT32: {
        if (wrenGetSlotType(vm, 2) != WREN_TYPE_NUM) {
          goto fail_cast;
        }
        int32_t* value = args[i];
        *value = floor(wrenGetSlotDouble(vm, 2));
      } break;
      case FFI_TYPE_POINTER: {
        if (wrenGetSlotType(vm, 2) == WREN_TYPE_STRING) {
          char** ptr = (char**)args[i];
          char* text = wrenGetSlotString(vm, 2);
          *ptr = text;
        } else if (wrenGetSlotType(vm, 2) == WREN_TYPE_FOREIGN) {
          // ASSUME POINTER
         void** ptr = (void**)wrenGetSlotForeign(vm, 2);
         void** value = args[i];
         *value = *ptr;
        } else {
          goto fail_cast;
        }
      } break;
      case FFI_TYPE_STRUCT: {
        if (wrenGetSlotType(vm, 2) == WREN_TYPE_FOREIGN) {
          STRUCT* data = (STRUCT*)wrenGetSlotForeign(vm, 2);
          args[i] = &(data->blob);
        } else {
          goto fail_cast;
        }
      } break;
      case FFI_TYPE_COMPLEX:
      default: {
        fail_cast:
        wrenSetSlotString(vm, 1, "FFI: Argument mismatch");
        wrenAbortFiber(vm, 1);
        return;
      }
    }
  }

  void* returnValue = alloca(function->cif.rtype->size);
  ffi_call(&(function->cif), FFI_FN(function->methodPtr), returnValue, args);

  ffi_type* returnType = function->cif.rtype;
  switch (returnType->type) {
    case FFI_TYPE_UINT8: {
      uint8_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT8: {
      int8_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT16: {
      uint16_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT16: {
      int16_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT32: {
      uint32_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT32: {
      int32_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT64: {
      uint64_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT64: {
      int64_t* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_FLOAT: {
      float* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_DOUBLE: {
      double* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_LONGDOUBLE: {
      long double* ptr = returnValue;
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_POINTER: {
      void** ptr = returnValue;
      wrenGetVariable(vm, "ffi", "Pointer", 0);
      void** obj = wrenSetSlotNewForeign(vm, 0, 0, sizeof(void*));
      *obj = *ptr;
    } break;
    case FFI_TYPE_VOID:
    default: wrenSetSlotNull(vm, 0); break;
  }
}


internal void
STRUCT_TYPE_allocate(WrenVM* vm) {
  wrenEnsureSlots(vm, 3);
  size_t elementCount = wrenGetListCount(vm, 1);

  STRUCT_TYPE* type = wrenSetSlotNewForeign(vm, 0, 0, sizeof(STRUCT_TYPE) + (elementCount + 1) * sizeof(ffi_type*));
  type->elementCount = elementCount;
  for (size_t i = 0; i < elementCount; i++) {
    wrenGetListElement(vm, 1, i, 2);
    WrenType slotType = wrenGetSlotType(vm, 2);
    printf("%s\n", DEBUG_printWrenType(slotType));
    if (slotType == WREN_TYPE_STRING) {
      char* typeName = wrenGetSlotString(vm, 2);
      type->elements[i] = toFFIType(typeName);
    } else if (slotType == WREN_TYPE_FOREIGN) {
      STRUCT_TYPE* structType = wrenGetSlotForeign(vm, 2);
      type->elements[i] = &(structType->typeData);
    } else {
      wrenSetSlotString(vm, 1, "Invalid Struct Element");
      wrenAbortFiber(vm, 1);
      return;
    }
  }
  type->elements[elementCount] = NULL;
  // Set up struct
  type->typeData.size = 0;
  type->typeData.alignment = 0;
  type->typeData.elements = type->elements;
  type->typeData.type = FFI_TYPE_STRUCT;

  if (ffi_get_struct_offsets(FFI_DEFAULT_ABI, &(type->typeData), NULL) != FFI_OK) {
    VM_ABORT(vm, "Improper Struct Type");
  }
}

internal void
STRUCT_TYPE_finalize(void* data) {

}

internal void
STRUCT_TYPE_getOffset(WrenVM* vm) {

}


internal void
STRUCT_allocate(WrenVM* vm) {
  STRUCT_TYPE* dataType = wrenGetSlotForeign(vm, 1);
  printf("%li\n", dataType->typeData.size);
  STRUCT* data = wrenSetSlotNewForeign(vm, 0, 0, sizeof(STRUCT) + dataType->typeData.size);
  data->dataType = dataType;
  data->start = (uint8_t*)&data->blob;
  size_t elementCount = dataType->elementCount;
  size_t offsets[elementCount];

  int status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, &(dataType->typeData), offsets);
  if (status != FFI_OK) {
    VM_ABORT(vm, "Invalid Struct");
    return;
  }
  for (size_t i = 0; i < elementCount; i++) {
    // Move element i from List to slot 3
    wrenGetListElement(vm, 2, i, 1);
    if (wrenGetSlotType(vm, 1) != WREN_TYPE_UNKNOWN) {
      ffi_type* element = dataType->elements[i];
      switch(element->type) {
        case FFI_TYPE_FLOAT: {
          float* ptr = (float*)(data->start + offsets[i]);
          *ptr = wrenGetSlotDouble(vm, 1);
        } break;
        case FFI_TYPE_DOUBLE: {
          double* ptr = (double*)(data->start + offsets[i]);
          *ptr = wrenGetSlotDouble(vm, 1);
        } break;
        case FFI_TYPE_INT: {
          int* ptr = (int*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_UINT8: {
          uint8_t* ptr = (uint8_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_SINT8: {
          int8_t* ptr = (int8_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_UINT16: {
          uint16_t* ptr = (uint16_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_SINT16: {
          int16_t* ptr = (int16_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_UINT32: {
          uint32_t* ptr = (uint32_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_SINT32: {
          int32_t* ptr = (int32_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_UINT64: {
          uint64_t* ptr = (uint64_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_SINT64: {
          int64_t* ptr = (int64_t*)(data->start + offsets[i]);
          *ptr = floor(wrenGetSlotDouble(vm, 1));
        } break;
        case FFI_TYPE_STRUCT: {
          uint8_t* ptr = (uint8_t*)(data->start + offsets[i]);
          STRUCT* param = wrenGetSlotForeign(vm, 1);
          memcpy(ptr, param->start, param->dataType->typeData.size);
        } break;
        case FFI_TYPE_POINTER: {
          void** ptr = (void**)(data->start + offsets[i]);
          void** param = wrenGetSlotForeign(vm, 1);
          *ptr = *param;
        } break;
        case FFI_TYPE_COMPLEX:
        default:
          VM_ABORT(vm, "Unsupported");
          return;
      }
    } else {
      VM_ABORT(vm, "Invalid initialiser value");
    }
  }
}

internal void
STRUCT_getValue(WrenVM* vm) {
  STRUCT* data = wrenGetSlotForeign(vm, 0);
  STRUCT_TYPE* dataType = data->dataType;
  size_t index = wrenGetSlotDouble(vm, 1);
  size_t offsets[dataType->elementCount];

  int status = ffi_get_struct_offsets(FFI_DEFAULT_ABI, &(dataType->typeData), offsets);
  if (status != FFI_OK) {
    VM_ABORT(vm, "Invalid Struct");
    return;
  }

  ffi_type* element = dataType->elements[index];
  switch(element->type) {
    case FFI_TYPE_LONGDOUBLE: {
      long double* ptr = (long double*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_DOUBLE: {
      double* ptr = (double*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_FLOAT: {
      float* ptr = (float*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT8: {
      int8_t* ptr = (int8_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT8: {
      uint8_t* ptr = (uint8_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT16: {
      int16_t* ptr = (int16_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT16: {
      uint16_t* ptr = (uint16_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT32: {
      int32_t* ptr = (int32_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT32: {
      uint32_t* ptr = (uint32_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_SINT64: {
      int64_t* ptr = (int64_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;
    case FFI_TYPE_UINT64: {
      uint64_t* ptr = (uint64_t*)(data->start + offsets[index]);
      wrenSetSlotDouble(vm, 0, *ptr);
    } break;

    case FFI_TYPE_POINTER: {
      void* ptr = (data->start + offsets[index]);
      wrenGetVariable(vm, "ffi", "Pointer", 0);
      void** obj = wrenSetSlotNewForeign(vm, 0, 0, sizeof(void*));
      *obj = ptr;
    } break;

    case FFI_TYPE_STRUCT: { // handle nested structs
      void* ptr = (data->start + offsets[index]);
      wrenGetVariable(vm, "ffi", "Struct", 0);
      STRUCT_TYPE* newType = (STRUCT_TYPE*)dataType->elements[index];
      STRUCT* newStruct = (STRUCT*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(STRUCT) + newType->typeData.size);
      newStruct->dataType = newType;
      newStruct->start = (uint8_t*)&newStruct->blob;
      memcpy(newStruct->start, ptr, newType->typeData.size);
    } break;
    case FFI_TYPE_COMPLEX:
    case FFI_TYPE_VOID:
    default: wrenSetSlotNull(vm, 0); break;

  }

}

internal void
STRUCT_finalize(void* data) {

}

internal void
POINTER_allocate(WrenVM* vm) {
  void** obj = wrenSetSlotNewForeign(vm, 0, 0, sizeof(void*));
  *obj = NULL;
}

internal void
POINTER_reserve(WrenVM* vm) {
  POINTER_allocate(vm);
  void** obj = wrenGetSlotForeign(vm, 0);
  size_t bytes = wrenGetSlotDouble(vm, 1);
  *obj = malloc(bytes);
  memset(*obj, 0, bytes);
}

internal void
POINTER_free(WrenVM* vm) {
  void** obj = wrenGetSlotForeign(vm, 0);
  free(*obj);
  wrenSetSlotNull(vm, 0);
}

internal void
POINTER_asString(WrenVM* vm) {
  void** obj = wrenGetSlotForeign(vm, 0);
  wrenSetSlotString(vm, 0, *obj);
}

internal void
POINTER_asBytes(WrenVM* vm) {
  void** obj = wrenGetSlotForeign(vm, 0);
  size_t size = wrenGetSlotDouble(vm, 1);
  wrenSetSlotBytes(vm, 0, *obj, size);
}

#endif
