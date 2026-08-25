/* =============
Copyright (c) 2026, STMicroelectronics

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted
provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions
  and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of
  conditions and the following disclaimer in the documentation and/or other materials provided
  with the distribution.

* Neither the name of the copyright holders nor the names of its contributors may be used to
  endorse or promote products derived from this software without specific prior written
  permission.

*THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER /
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.*
*/


#ifndef NANOEDGEAI_H
#define NANOEDGEAI_H

#include <stdint.h>

/* NEAI ID */
#define NEAI_ID "6a4246c4d0d534775b0500da"

/* Input signal configuration */
#define NEAI_INPUT_SIGNAL_LENGTH 255
#define NEAI_INPUT_AXIS_NUMBER 3


/* Classification configuration */
#define NEAI_NUMBER_OF_CLASSES 4

/* NEAI State Enum */
enum neai_state {
	NEAI_OK = 0,
	NEAI_ERROR = 1,
	NEAI_NOT_INITIALIZED = 2,
	NEAI_INVALID_PARAM = 3,
	NEAI_NOT_SUPPORTED = 4,
	NEAI_LEARNING_DONE = 5,
	NEAI_LEARNING_IN_PROGRESS = 6
};


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Must be called at the beginning to initialize the classification model by loading the
 *         pretrained model.
 * @return NEAI_OK on success, error code otherwise.
 */
enum neai_state neai_classification_init(void);

/**
 * @brief  Perform classification on a new input sample by returning the probability for each
 *         class and the predicted class ID.
 * @param  in             [in]   Pointer to the input signal array
 *                               (size NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER).
 * @param  probabilities  [out]  Pointer to the output probabilities array
 *                               (size NEAI_NUMBER_OF_CLASSES).
 * @param  id_class       [out]  Pointer to the predicted class ID
 *                               (integer in range [0, NEAI_NUMBER_OF_CLASSES - 1]).
 * @return NEAI_OK on success.
 *         Error code otherwise.
 */
enum neai_state neai_classification(float *in, float *probabilities, int *id_class);

/* ===== Common getter functions ===== */
/**
 * @brief  Get the NEAI identifier.
 * @return Pointer to a string containing the NEAI ID.
 */
char* neai_get_id(void);

/**
 * @brief  Get the input signal size (number of samples per axis).
 * @return Input signal size.
 */
int neai_get_input_signal_size(void);

/**
 * @brief  Get the number of input axes/channels.
 * @return Number of input axes.
 */
int neai_get_axis_number(void);


/* ===== Specific classification getter functions ===== */
/**
 * @brief  Get the number of classes in the classification model.
 * @return Number of classes.
 */
int neai_get_number_of_classes(void);

/**
 * @brief  Get the class name for a given class ID.
 * @param  id_class [in]  Class ID
 *                        (integer in range [0, NEAI_NUMBER_OF_CLASSES - 1]).
 * @return Pointer to the class name string, or NULL if the ID is invalid.
 */
const char* neai_get_class_name(int id_class);


#ifdef __cplusplus
}
#endif

#endif /* NANOEDGEAI_H */


/* =============
Declarations to add to your main program to use the NanoEdge AI library.
You may copy-paste them directly or rename variables as needed.
WARNING: Respect the structures, types, and buffer sizes; only variable names may be changed.

enum neai_state state;   // Captures return states from NEAI functions
int id_class;   // Predicted class ID returned by classification function
float input_signal[NEAI_INPUT_SIGNAL_LENGTH * NEAI_INPUT_AXIS_NUMBER];   // Input signal buffer
float probabilities[NEAI_NUMBER_OF_CLASSES];   // Output probabilities buffer returned by classification function
============= */
