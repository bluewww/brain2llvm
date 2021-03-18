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
#include <llvm-c/Error.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bf_interpreter.h"

#define BF_MEM_SZ (64 * 1024)

int
handle_error(LLVMErrorRef err)
{
	char *msg = LLVMGetErrorMessage(err);
	fprintf(stderr, "error: %s\n", msg);
	LLVMDisposeErrorMessage(msg);
	return 1;
}

/* lower brainfuck to llvm */
void
lower(char *prog, LLVMModuleRef mod, LLVMContextRef ctx, bool trace)
{

	/* link putchar() and getchar() externally */
	LLVMTypeRef putchar_args[] = { LLVMInt32TypeInContext(ctx) };
	LLVMTypeRef getchar_args[] = {};

	LLVMTypeRef putchar_type = LLVMFunctionType(
	    LLVMInt32TypeInContext(ctx), putchar_args, 1, false);
	LLVMTypeRef getchar_type = LLVMFunctionType(
	    LLVMInt32TypeInContext(ctx), getchar_args, 0, false);

	LLVMValueRef putchar_fun = LLVMAddFunction(
	    mod, "putchar", putchar_type);
	LLVMValueRef getchar_fun = LLVMAddFunction(
	    mod, "getchar", getchar_type);

	LLVMSetLinkage(putchar_fun, LLVMExternalLinkage);
	LLVMSetLinkage(getchar_fun, LLVMExternalLinkage);

	/* add jitted function */
	LLVMTypeRef jitted_args[] = {};
	LLVMTypeRef jitted_type = LLVMFunctionType(
	    LLVMVoidTypeInContext(ctx), jitted_args, 0, false);
	LLVMValueRef jitted_fun = LLVMAddFunction(mod, "jitted", jitted_type);

	LLVMSetLinkage(jitted_fun, LLVMExternalLinkage);

	LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(
	    ctx, jitted_fun, "entry");

	LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
	LLVMPositionBuilderAtEnd(builder, entry_bb);

	/* create tape memory on stack and zero it */
	LLVMValueRef mem = LLVMBuildArrayAlloca(builder,
	    LLVMInt8TypeInContext(ctx),
	    LLVMConstInt(LLVMInt32TypeInContext(ctx), BF_MEM_SZ, false), "mem");
	LLVMBuildMemSet(builder, mem,
	    LLVMConstInt(LLVMInt8TypeInContext(ctx), 0, false),
	    LLVMConstInt(LLVMInt32TypeInContext(ctx), BF_MEM_SZ, false), 0);

	/* tape pointer, init to zero */
	LLVMValueRef tape_ptr = LLVMBuildAlloca(
	    builder, LLVMInt32TypeInContext(ctx), "tape_ptr");
	LLVMBuildStore(builder,
	    LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false), tape_ptr);

	while (*prog) {

		LLVMValueRef gep_args[1] = { 0 };
		LLVMValueRef call_args[1] = { 0 };
		LLVMValueRef load, decr, incr;
		LLVMValueRef ele_ptr, load_ele, incr_ele, decr_ele;
		LLVMValueRef cast, offset;
		LLVMValueRef user;

		if (trace)
			printf("lower: lowering '%c'\n", *prog);

		switch (*prog) {
		case ',':
			/* getchar */
			call_args[0] = 0;
			user = LLVMBuildCall(
			    builder, getchar_fun, call_args, 0, "call_comma");
			cast = LLVMBuildIntCast2(builder, user,
			    LLVMInt8TypeInContext(ctx), false, "cast_int2char");
			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			LLVMBuildStore(builder, cast, ele_ptr);

			prog++;
			break;
		case '.':
			/* putchar. Note we need to cast char to int */
			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			load_ele = LLVMBuildLoad2(builder,
			    LLVMInt8TypeInContext(ctx), ele_ptr, "load_ele");
			cast = LLVMBuildIntCast2(builder, load_ele,
			    LLVMInt32TypeInContext(ctx), false,
			    "cast_char2int");
			call_args[0] = cast;
			LLVMBuildCall(
			    builder, putchar_fun, call_args, 1, "call_dot");

			prog++;
			break;
		case '-':
			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			load_ele = LLVMBuildLoad2(builder,
			    LLVMInt8TypeInContext(ctx), ele_ptr, "load_ele");
			decr_ele = LLVMBuildSub(builder, load_ele,
			    LLVMConstInt(LLVMInt8TypeInContext(ctx), 1, false),
			    "decr_ele");
			LLVMBuildStore(builder, decr_ele, ele_ptr);

			prog++;
			break;
		case '+':
			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			load_ele = LLVMBuildLoad2(builder,
			    LLVMInt8TypeInContext(ctx), ele_ptr, "load_ele");
			incr_ele = LLVMBuildAdd(builder, load_ele,
			    LLVMConstInt(LLVMInt8TypeInContext(ctx), 1, false),
			    "incr_ele");
			LLVMBuildStore(builder, incr_ele, ele_ptr);

			prog++;
			break;
		case '<':
			/* decrement pointed value */
			load = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "load");
			decr = LLVMBuildSub(builder, load,
			    LLVMConstInt(LLVMInt32TypeInContext(ctx), 1, false),
			    "decr");
			LLVMBuildStore(builder, decr, tape_ptr);

			prog++;
			break;
		case '>':
			/* increment pointed value */
			load = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "load");
			incr = LLVMBuildAdd(builder, load,
			    LLVMConstInt(LLVMInt32TypeInContext(ctx), 1, false),
			    "incr");
			LLVMBuildStore(builder, incr, tape_ptr);

			prog++;
			break;
		case '[':
			prog++;
			break;
		case ']':
			break;
		case ' ':
		case '\n':
		case '\t':
			prog++;
			break;
		default:
			fprintf(stderr, "bf: bad character '%c'\n", *prog);
			abort();
			break;
		}

		prog++;
	}

	/* no return value */
	LLVMBuildRetVoid(builder);

	/* verify what we are doing */
	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);
}

int
main(void)
{
	int status = EXIT_SUCCESS;

	LLVMOrcThreadSafeContextRef tsctx = LLVMOrcCreateNewThreadSafeContext();
	LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(tsctx);

	LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("brain", ctx);

	/* lower program to llvm ir */
	lower(
	    "++++++++ ++++++++ ++++++++ ++++++++ ++++++++ ++++++++ >+++++ [<+.>-]",
	    mod, ctx, true);

	if (LLVMWriteBitcodeToFile(mod, "brain2llvm.bc")) {
		fprintf(stderr, "error writing bitcode to file\n");
		abort();
	}

	return status;

	LLVMOrcThreadSafeModuleRef tsm = LLVMOrcCreateNewThreadSafeModule(
	    mod, tsctx);

	/* char *error = NULL; */
	/* LLVMVerifyModule(mod, LLVMAbortProcessAction, &error); */
	/* LLVMDisposeMessage(error); */

	LLVMInitializeCore(LLVMGetGlobalPassRegistry());

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmParser();
	LLVMInitializeNativeAsmPrinter();

/* 	/\* create jit instance *\/ */
/* 	LLVMOrcLLJITRef lljit; */
/* 	LLVMErrorRef err; */
/* 	if ((err = LLVMOrcCreateLLJIT(&lljit, 0))) { */
/* 		status = handle_error(err); */
/* 		goto orc_llvm_fail; */
/* 	} */

/* 	/\* add module to jit instance *\/ */
/* 	LLVMOrcJITDylibRef mainjd = LLVMOrcLLJITGetMainJITDylib(lljit); */
/* 	if ((err = LLVMOrcLLJITAddLLVMIRModule(lljit, mainjd, tsm))) { */
/* 		LLVMOrcDisposeThreadSafeModule(tsm); */
/* 		status = handle_error(err); */
/* 		goto module_add_fail; */
/* 	} */

/* 	/\* look up address of sum function *\/ */
/* 	LLVMOrcJITTargetAddress sum_addr; */
/* 	if ((err = LLVMOrcLLJITLookup(lljit, &sum_addr, "sum"))) { */
/* 		status = handle_error(err); */
/* 		goto sum_addr_fail; */
/* 	} */

/* 	int (*sum_ptr)(int, int) = (int (*)(int, int))sum_addr; */

/* 	printf("%d\n", sum_ptr(x, y)); */

/* module_add_fail: */
/* sum_addr_fail: */
/* 	/\* destroy jit instance. This may fail! *\/ */
/* 	if ((err = LLVMOrcDisposeLLJIT(lljit))) { */
/* 		int new_err = handle_error(err); */
/* 		if (status == 0) */
/* 			status = new_err; */
/* 	} */

/* orc_llvm_fail: */
/* 	LLVMShutdown(); */

	return status;
}
