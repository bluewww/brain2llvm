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
#include <llvm-c/Orc.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <llvm-c/Error.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bf_interpreter.h"

int
handle_error(LLVMErrorRef err)
{
	char *msg = LLVMGetErrorMessage(err);
	fprintf(stderr, "error: %s\n", msg);
	LLVMDisposeErrorMessage(msg);
	return 1;
}

int
sum(int x, int y)
{
	return x + y;
}

int
main(int argc, char **argv)
{
	int status = EXIT_SUCCESS;

	if (argc < 3) {
		fprintf(stderr, "usage: %s x y\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	long long x = strtoll(argv[1], NULL, 0);
	long long y = strtoll(argv[2], NULL, 0);

	LLVMOrcThreadSafeContextRef tsctx = LLVMOrcCreateNewThreadSafeContext();
	LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(tsctx);

	LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("brain", ctx);

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

	if (LLVMWriteBitcodeToFile(mod, "brain2llvm.bc")) {
		fprintf(stderr, "error writing bitcode to file\n");
		abort();
	}

	LLVMOrcThreadSafeModuleRef tsm = LLVMOrcCreateNewThreadSafeModule(
	    mod, tsctx);

	/* char *error = NULL; */
	/* LLVMVerifyModule(mod, LLVMAbortProcessAction, &error); */
	/* LLVMDisposeMessage(error); */

	LLVMInitializeCore(LLVMGetGlobalPassRegistry());

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmParser();
	LLVMInitializeNativeAsmPrinter();

	/* create jit instance */
	LLVMOrcLLJITRef lljit;
	LLVMErrorRef err;
	if ((err = LLVMOrcCreateLLJIT(&lljit, 0))) {
		status = handle_error(err);
		goto orc_llvm_fail;
	}

	/* add module to jit instance */
	LLVMOrcJITDylibRef mainjd = LLVMOrcLLJITGetMainJITDylib(lljit);
	if ((err = LLVMOrcLLJITAddLLVMIRModule(
		 lljit, mainjd, tsm))) {
		LLVMOrcDisposeThreadSafeModule(tsm);
		status = handle_error(err);
		goto module_add_fail;
	}

	/* look up address of sum function */
	LLVMOrcJITTargetAddress sum_addr;
	if ((err = LLVMOrcLLJITLookup(lljit, &sum_addr, "sum"))) {
		status = handle_error(err);
		goto sum_addr_fail;
	}

	int (*sum_ptr)(int, int) = (int (*)(int, int))sum_addr;

	printf("%d\n", sum_ptr(x, y));


module_add_fail:
sum_addr_fail:
	/* destroy jit instance. This may fail! */
	if ((err = LLVMOrcDisposeLLJIT(lljit))) {
		int new_err = handle_error(err);
		if (status == 0)
			status = new_err;
	}

orc_llvm_fail:
	LLVMShutdown();

	return status;
}
