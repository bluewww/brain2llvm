/*
 * Copyright 2021 ETH Zurich
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
 *
 * SPDX-License-Identifier: Apache-2.0
 * Author: Robert Balas (balasr@iis.ee.ethz.ch)
 */

#include <errno.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bf_interpreter.h"

int
sum(int x, int y)
{
	return x + y;
}

int
main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s x y\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	long long x = strtoll(argv[1], NULL, 0);
	long long y = strtoll(argv[2], NULL, 0);

	LLVMModuleRef mod = LLVMModuleCreateWithName("brain");

	LLVMTypeRef param_types[] = { LLVMInt32Type(), LLVMInt32Type() };
	LLVMTypeRef ret_type = LLVMFunctionType(
	    LLVMInt32Type(), param_types, 2, false);
	LLVMValueRef sum = LLVMAddFunction(mod, "sum", ret_type);

	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(sum, "entry");

	LLVMBuilderRef builder = LLVMCreateBuilder();
	LLVMPositionBuilderAtEnd(builder, entry);

	LLVMValueRef tmp = LLVMBuildAdd(
	    builder, LLVMGetParam(sum, 0), LLVMGetParam(sum, 1), "tmp");
	LLVMBuildRet(builder, tmp);

	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMExecutionEngineRef engine;
	error = NULL;
	LLVMLinkInMCJIT();
	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmParser();
	LLVMInitializeNativeAsmPrinter();

	/* != 0 is not the same as ! */
	if (LLVMCreateExecutionEngineForModule(&engine, mod, &error) != 0) {
		fprintf(stderr, "failed to create execution engine\n");
		abort();
	}

	if (error) {
		fprintf(stderr, "error:%s\n", error);
		LLVMDisposeMessage(error);
		exit(EXIT_FAILURE);
	}

	int (*sum_ptr)(int, int) = (int (*)(int, int))LLVMGetFunctionAddress(
	    engine, "sum");

	printf("%d\n", sum_ptr(x, y));

	if (LLVMWriteBitcodeToFile(mod, "brain2llvm.bc") != 0) {
		fprintf(stderr, "error writing bitcode to file\n");
		abort();
	}

	LLVMDisposeModule(mod);
	return 0;
}
