#if 0
// Copyright (C) 2023-2024 National Center for Atmospheric Research
// SPDX-License-Identifier: Apache-2.0

// This file defines error categories and codes for MICM
#endif

#define MICM_ERROR_CATEGORY_CONFIGURATION                       "MICM Configuration"
#define MICM_CONFIGURATION_ERROR_CODE_INVALID_KEY               1
#define MICM_CONFIGURATION_ERROR_CODE_UNKNOWN_KEY               2
#define MICM_CONFIGURATION_ERROR_CODE_INVALID_FILE_PATH         3
#define MICM_CONFIGURATION_ERROR_CODE_NO_CONFIG_FILES_FOUND     4
#define MICM_CONFIGURATION_ERROR_CODE_CAMP_FILES_NOT_FOUND      5
#define MICM_CONFIGURATION_ERROR_CODE_CAMP_DATA_NOT_FOUND       6
#define MICM_CONFIGURATION_ERROR_CODE_INVALID_SPECIES           7
#define MICM_CONFIGURATION_ERROR_CODE_INVALID_MECHANISM         8
#define MICM_CONFIGURATION_ERROR_CODE_INVALID_TYPE              9
#define MICM_CONFIGURATION_ERROR_CODE_OBJECT_TYPE_NOT_FOUND     10
#define MICM_CONFIGURATION_ERROR_CODE_REQUIRED_KEY_NOT_FOUND    11
#define MICM_CONFIGURATION_ERROR_CODE_CONTAINS_NON_STANDARD_KEY 12
#define MICM_CONFIGURATION_ERROR_CODE_MUTUALLY_EXCLUSIVE_OPTION 13

#define MICM_ERROR_CATEGORY_JIT                  "MICM JIT"
#define MICM_JIT_ERROR_CODE_INVALID_MATRIX       1
#define MICM_JIT_ERROR_CODE_MISSING_JIT_FUNCTION 2
#define MICM_JIT_ERROR_CODE_FAILED_TO_BUILD      3

#define MICM_ERROR_CATEGORY_PROCESS                                     "MICM Process"
#define MICM_PROCESS_ERROR_CODE_TOO_MANY_REACTANTS_FOR_SURFACE_REACTION 1

#define MICM_ERROR_CATEGORY_PROCESS_SET                     "MICM Process Set"
#define MICM_PROCESS_SET_ERROR_CODE_REACTANT_DOES_NOT_EXIST 1
#define MICM_PROCESS_SET_ERROR_CODE_PRODUCT_DOES_NOT_EXIST  2

#define MICM_ERROR_CATEGORY_RATE_CONSTANT                                              "MICM Rate Constant"
#define MICM_RATE_CONSTANT_ERROR_CODE_MISSING_ARGUMENTS_FOR_SURFACE_RATE_CONSTANT      1
#define MICM_RATE_CONSTANT_ERROR_CODE_MISSING_ARGUMENTS_FOR_USER_DEFINED_RATE_CONSTANT 2

#define MICM_ERROR_CATEGORY_SPECIES                       "MICM Species"
#define MICM_SPECIES_ERROR_CODE_PROPERTY_NOT_FOUND        1
#define MICM_SPECIES_ERROR_CODE_INVALID_TYPE_FOR_PROPERTY 2

#define MICM_ERROR_CATEGORY_MATRIX                  "MICM Matrix"
#define MICM_MATRIX_ERROR_CODE_ROW_SIZE_MISMATCH    1
#define MICM_MATRIX_ERROR_CODE_INVALID_VECTOR       2
#define MICM_MATRIX_ERROR_CODE_ELEMENT_OUT_OF_RANGE 3
#define MICM_MATRIX_ERROR_CODE_MISSING_BLOCK_INDEX  4
#define MICM_MATRIX_ERROR_CODE_ZERO_ELEMENT_ACCESS  5

#define MICM_ERROR_CATEGORY_CUDA_SOLVER                          "MICM CUDA Rosenbrock Solver"
#define MICM_CUDA_ROSENBROCK_SOLVER_ERROR_CODE_PARAMTER_MISMATCH 1
#define MICM_CUDA_ROSENBROCK_SOLVER_ERROR_CODE_CUBLAS_ERROR      2

#define MICM_ERROR_CATEGORY_INTERNAL     "MICM Internal Error"
#define MICM_INTERNAL_ERROR_CODE_GENERAL 1
#define MICM_INTERNAL_ERROR_CODE_CUDA    2
#define MICM_INTERNAL_ERROR_CODE_CUBLAS  3