/*******************************************************************************
 * Copyright 2013-2014 Aerospike, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include <Python.h>
#include <stdbool.h>

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_udf.h>
#include <aerospike/as_config.h>
#include <aerospike/as_error.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_udf.h>

#include "client.h"
#include "conversions.h"
#include "exceptions.h"
#include "policy.h"

#define SCRIPT_LEN_MAX 1048576

/**
 *******************************************************************************************************
 * Registers a UDF module with the Aerospike DB.
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns an integer status. 0(Zero) is success value.
 * In case of error,appropriate exceptions will be raised.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_UDF_Put(AerospikeClient * self, PyObject *args, PyObject * kwds)
{
	// Initialize error
	as_error err;
	as_error_init(&err);

	// Python Function Arguments
	PyObject * py_filename = NULL;
	long language = 0;
	PyObject * py_udf_type = NULL;
	PyObject * py_policy = NULL;
	PyObject * py_ustr = NULL;

	uint8_t * bytes = NULL;
	as_policy_info info_policy;
	as_policy_info *info_policy_p = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"filename", "udf_type", "policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "O|lO:udf_put", kwlist,
				&py_filename, &language, &py_policy) == false ) {
		return NULL;
	}

	if(language != AS_UDF_TYPE_LUA)
	{
		as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Invalid UDF language");
		goto CLEANUP;
	}
	py_udf_type = PyLong_FromLong(language);

	if (!self || !self->as) {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Invalid aerospike object");
		goto CLEANUP;
	}

	if (!self->is_conn_16) {
		as_error_update(&err, AEROSPIKE_ERR_CLUSTER, "No connection to aerospike cluster");
		goto CLEANUP;
	}

	// Convert PyObject into a filename string
	char *filename = NULL;
	if (PyUnicode_Check(py_filename)) {
		py_ustr = PyUnicode_AsUTF8String(py_filename);
		filename = PyString_AsString(py_ustr);
	} else if (PyString_Check(py_filename)) {
		filename = PyString_AsString(py_filename);
	} else {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Filename should be a string");
		goto CLEANUP;
	}

	// Convert python object to policy_info
	pyobject_to_policy_info( &err, py_policy, &info_policy, &info_policy_p,
			&self->as->config.policies.info);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}
	as_udf_type udf_type = (as_udf_type)PyInt_AsLong(py_udf_type);

	// Convert lua file to content
	as_bytes content;
	FILE * file = fopen(filename,"r");

	if ( !file ) {
		as_error_update(&err, AEROSPIKE_ERR_LUA_FILE_NOT_FOUND, "cannot open script file");
		goto CLEANUP;
	}

	bytes = (uint8_t *) malloc(SCRIPT_LEN_MAX);
	if ( bytes == NULL ) {
		as_error_update(&err, errno, "malloc failed");
		goto CLEANUP;
	}

	int size = 0;

	uint8_t * buff = bytes;
	int read = (int)fread(buff, 1, 512, file);
	while ( read ) {
		size += read;
		buff += read;
		read = (int)fread(buff, 1, 512, file);
	}
	fclose(file);

	as_bytes_init_wrap(&content, bytes, size, true);

	// Invoke operation
	aerospike_udf_put(self->as, &err, info_policy_p, filename, udf_type, &content);
	if( err.code != AEROSPIKE_OK ) {
		as_error_update(&err, err.code, NULL);
		goto CLEANUP;
	} else {
		aerospike_udf_put_wait(self->as, &err, info_policy_p, as_basename(NULL, filename), 2000);
	}

CLEANUP:
	if(bytes) {
		free(bytes);
	}

	if (py_ustr) {
		Py_DECREF(py_ustr);
	}

	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyObject *exception_type = raise_exception(&err);
		if(PyObject_HasAttrString(exception_type, "module")) {
			PyObject_SetAttrString(exception_type, "module", Py_None);
		} 
		if(PyObject_HasAttrString(exception_type, "func")) {
			PyObject_SetAttrString(exception_type, "func", Py_None);
		}
		PyErr_SetObject(exception_type, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return PyLong_FromLong(0);
}

/**
 *******************************************************************************************************
 * Removes a UDF module from the Aerospike DB
 *
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns an integer status. 0(Zero) is success value.
 * In case of error,appropriate exceptions will be raised.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_UDF_Remove(AerospikeClient * self, PyObject *args, PyObject * kwds)
{
	// Initialize error
	as_error err;
	as_error_init(&err);

	// Python Function Arguments
	PyObject * py_policy = NULL;
	PyObject * py_filename = NULL;
	PyObject * py_ustr = NULL;

	as_policy_info info_policy;
	as_policy_info *info_policy_p = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"filename", "policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "O|O:udf_remove", kwlist,
				&py_filename, &py_policy) == false ) {
		return NULL;
	}

	if (!self || !self->as) {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Invalid aerospike object");
		goto CLEANUP;
	}

	if (!self->is_conn_16) {
		as_error_update(&err, AEROSPIKE_ERR_CLUSTER, "No connection to aerospike cluster");
		goto CLEANUP;
	}

	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}

	// Convert PyObject into a filename string
	char *filename = NULL;
	if (PyUnicode_Check(py_filename)) {
		py_ustr = PyUnicode_AsUTF8String(py_filename);
		filename = PyString_AsString(py_ustr);
	} else if (PyString_Check(py_filename)) {
		filename = PyString_AsString(py_filename);
	} else {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Filename should be a string");
		goto CLEANUP;
	}

	// Convert python object to policy_info
	pyobject_to_policy_info( &err, py_policy, &info_policy, &info_policy_p,
			&self->as->config.policies.info);

	// Invoke operation
	aerospike_udf_remove(self->as, &err, info_policy_p, filename);
	if ( err.code != AEROSPIKE_OK ) {
		as_error_update(&err, err.code, NULL);
		goto CLEANUP;
	}

CLEANUP:

	if (py_ustr) {
		Py_DECREF(py_ustr);
	}
	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyObject *exception_type = raise_exception(&err);
		if(PyObject_HasAttrString(exception_type, "module")) {
			PyObject_SetAttrString(exception_type, "module", py_filename);
		} 
		if(PyObject_HasAttrString(exception_type, "func")) {
			PyObject_SetAttrString(exception_type, "func", Py_None);
		}
		PyErr_SetObject(exception_type, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return PyLong_FromLong(0);
}

/**
 *******************************************************************************************************
 * Lists the UDF modules registered with the server
 *
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns list of modules that are registered with Aerospike DB.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_UDF_List(AerospikeClient * self, PyObject *args, PyObject * kwds)
{
	// Initialize error
	as_error err;
	as_error_init(&err);
	int init_udf_files = 0;

	// Python Function Arguments
	PyObject * py_policy = NULL;
	as_policy_info info_policy;
	as_policy_info *info_policy_p = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "O:udf_list", kwlist, &py_policy) == false ) {
		return NULL;
	}

	if (!self || !self->as) {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Invalid aerospike object");
		goto CLEANUP;
	}

	if (!self->is_conn_16) {
		as_error_update(&err, AEROSPIKE_ERR_CLUSTER, "No connection to aerospike cluster");
		goto CLEANUP;
	}

	// Convert python object to policy_info
	pyobject_to_policy_info( &err, py_policy, &info_policy, &info_policy_p,
			&self->as->config.policies.info);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}

	as_udf_files files;
	as_udf_files_init(&files, 0);
	init_udf_files = 1;

	// Invoke operation
	aerospike_udf_list(self->as, &err, info_policy_p, &files);
	if ( err.code != AEROSPIKE_OK ) {
		as_error_update(&err, err.code, NULL);
		goto CLEANUP;
	}

	// Convert as_udf_files struct into python object
	PyObject * py_files;
	as_udf_files_to_pyobject(&err, &files, &py_files);

	if ( err.code != AEROSPIKE_OK ) {
		as_error_update(&err, err.code, NULL);
		goto CLEANUP;
	}

CLEANUP:

	if (init_udf_files) {
		as_udf_files_destroy(&files);
	}

	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyObject *exception_type = raise_exception(&err);
		if(PyObject_HasAttrString(exception_type, "module")) {
			PyObject_SetAttrString(exception_type, "module", Py_None);
		} 
		if(PyObject_HasAttrString(exception_type, "func")) {
			PyObject_SetAttrString(exception_type, "func", Py_None);
		}
		PyErr_SetObject(exception_type, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return py_files;
}

/**
 *******************************************************************************************************
 * Gets the code for a UDF module registered with the server
 *
 * @param self                  AerospikeClient object
 * @param args                  The args is a tuple object containing an argument
 *                              list passed from Python to a C function
 * @param kwds                  Dictionary of keywords
 *
 * Returns the content of the UDF module.
 *******************************************************************************************************
 */
PyObject * AerospikeClient_UDF_Get_UDF(AerospikeClient * self, PyObject *args, PyObject * kwds)
{
	// Initialize error
	as_error err;
	as_error_init(&err);

	// Python Function Arguments
	PyObject * py_module = NULL;
	PyObject * py_policy = NULL;
	long language = 0;
	bool init_udf_file = false;
	PyObject * udf_content = NULL;
	PyObject * py_ustr = NULL;

	// Python Function Keyword Arguments
	static char * kwlist[] = {"module", "language", "policy", NULL};

	// Python Function Argument Parsing
	if ( PyArg_ParseTupleAndKeywords(args, kwds, "O|lO:udf_get", kwlist,
				&py_module ,&language, &py_policy) == false ) {
		return NULL;
	}

	if (!self || !self->as) {
		as_error_update(&err, AEROSPIKE_ERR_PARAM, "Invalid aerospike object");
		goto CLEANUP;
	}

	if (!self->is_conn_16) {
		as_error_update(&err, AEROSPIKE_ERR_CLUSTER, "No connection to aerospike cluster");
		goto CLEANUP;
	}

	if(language != AS_UDF_TYPE_LUA)
	{
		as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Invalid language");
		goto CLEANUP;
	}
	char* strModule = NULL;
	if ( PyUnicode_Check(py_module) ) {
		py_ustr = PyUnicode_AsUTF8String(py_module);
		strModule = PyString_AsString(py_ustr);
	}
	else if ( PyString_Check(py_module)){
		strModule = PyString_AsString(py_module);
	}
	else
	{
		as_error_update(&err, AEROSPIKE_ERR_CLIENT, "Module name should be a string or unicode string.");
		goto CLEANUP;
	}

	// Convert python object to policy_info
	as_policy_info *info_policy_p = NULL, info_policy;

	pyobject_to_policy_info( &err, py_policy, &info_policy, &info_policy_p,
			&self->as->config.policies.info);
	if ( err.code != AEROSPIKE_OK ) {
		goto CLEANUP;
	}

	as_udf_file file;
	as_udf_file_init(&file);
	init_udf_file=true;

	// Invoke operation
	aerospike_udf_get(self->as, &err, info_policy_p, strModule, (language - AS_UDF_TYPE_LUA) , &file);
	if ( err.code != AEROSPIKE_OK ) {
		as_error_update(&err, err.code, NULL);
		goto CLEANUP;
	}
	udf_content = Py_BuildValue("s#", file.content.bytes, file.content.size);

CLEANUP:

	if (py_ustr){
		Py_DECREF(py_ustr);
	}
	if(init_udf_file)
	{
		as_udf_file_destroy(&file);
	}
	if ( err.code != AEROSPIKE_OK ) {
		PyObject * py_err = NULL;
		error_to_pyobject(&err, &py_err);
		PyObject *exception_type = raise_exception(&err);
		if(PyObject_HasAttrString(exception_type, "module")) {
			PyObject_SetAttrString(exception_type, "module", py_module);
		} 
		if(PyObject_HasAttrString(exception_type, "func")) {
			PyObject_SetAttrString(exception_type, "func", Py_None);
		}
		PyErr_SetObject(exception_type, py_err);
		Py_DECREF(py_err);
		return NULL;
	}

	return udf_content;
}
