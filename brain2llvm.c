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
#include <unistd.h>

#define BF_MEM_SZ (64 * 1024)
#define BB_STACK_SZ (64 * 1024)

LLVMBasicBlockRef bb_stack[BB_STACK_SZ] = { 0 };

int
handle_error(LLVMErrorRef err)
{
	char *msg = LLVMGetErrorMessage(err);
	fprintf(stderr, "error: %s\n", msg);
	LLVMDisposeErrorMessage(msg);
	return 1;
}

void
print_bb(LLVMValueRef fun)
{
	LLVMBasicBlockRef bb = NULL;
	for (bb = LLVMGetFirstBasicBlock(fun); bb;
	     bb = LLVMGetNextBasicBlock(bb)) {
		printf("bb: %s\n", LLVMGetBasicBlockName(bb));
		if (LLVMGetBasicBlockTerminator(bb))
			puts("ok ");
		else
			puts("NO TERMINATOR");

		LLVMValueRef insn = NULL;
		for (insn = LLVMGetFirstInstruction(bb); insn;
		     insn = LLVMGetNextInstruction(insn)) {
			printf("insn: %d\n", LLVMGetInstructionOpcode(insn));
		}
	}
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

	int bb_index = 0;

	while (*prog) {

		LLVMValueRef gep_args[1] = { 0 };
		LLVMValueRef call_args[1] = { 0 };
		LLVMValueRef load, decr, incr;
		LLVMValueRef ele_ptr, load_ele, incr_ele, decr_ele;
		LLVMValueRef cast, offset;
		LLVMValueRef user;
		LLVMValueRef cmp;

		LLVMBasicBlockRef loop_bb = NULL;
		LLVMBasicBlockRef exit_bb = NULL;

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
			/* load value under tape_ptr */
			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			load_ele = LLVMBuildLoad2(builder,
			    LLVMInt8TypeInContext(ctx), ele_ptr, "load_ele");

			/* branch depending whether it's zero or not */
			cmp = LLVMBuildICmp(builder, LLVMIntEQ, load_ele,
			    LLVMConstInt(LLVMInt8TypeInContext(ctx), 0, false),
			    "cmp_zero");

			/* creat loop body block and skip block */
			loop_bb = LLVMAppendBasicBlockInContext(
			    ctx, jitted_fun, "loop_body");
			exit_bb = LLVMAppendBasicBlockInContext(
			    ctx, jitted_fun, "loop_exit");

			/* if cmp is zero, then exit loop, else loop */
			LLVMBuildCondBr(builder, cmp, exit_bb, loop_bb);

			/* push loop and exit to stack for nesting  of [ */
			if (bb_index >= BB_STACK_SZ - 2) {
				fprintf(
				    stderr, "bf: basic block stack overflow\n");
				abort();
			}
			bb_stack[bb_index++] = loop_bb;
			bb_stack[bb_index++] = exit_bb;

			/* continue inserting bb's to loop body */
			LLVMPositionBuilderAtEnd(builder, loop_bb);

			prog++;
			break;
		case ']':
			if (bb_index == 0) {
				fprintf(stderr, "bf: unmatched closing ']'\n");
				abort();
			} else if (bb_index < 2) {
				fprintf(stderr,
				    "bf: basic block stack underflow\n");
				abort();
			}

			/* pop from stack */
			exit_bb = bb_stack[--bb_index];
			loop_bb = bb_stack[--bb_index];

			offset = LLVMBuildLoad2(builder,
			    LLVMInt32TypeInContext(ctx), tape_ptr, "offset");
			gep_args[0] = offset;
			ele_ptr = LLVMBuildInBoundsGEP2(builder,
			    LLVMInt8TypeInContext(ctx), mem, gep_args, 1,
			    "ele_ptr");
			load_ele = LLVMBuildLoad2(builder,
			    LLVMInt8TypeInContext(ctx), ele_ptr, "load_ele");

			/* branch depending whether it's zero or not */
			cmp = LLVMBuildICmp(builder, LLVMIntNE, load_ele,
			    LLVMConstInt(LLVMInt8TypeInContext(ctx), 0, false),
			    "cmp_not_zero");

			/* if cmp is zero, then exit loop, else loop */
			LLVMBuildCondBr(builder, cmp, loop_bb, exit_bb);

			/* continue inserting bb's *after* loop body*/
			LLVMPositionBuilderAtEnd(builder, exit_bb);

			prog++;
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
	}

	/* no return value */
	LLVMBuildRetVoid(builder);

	if (trace)
		print_bb(jitted_fun);
}

void
usage(char **argv)
{
	fprintf(stderr, "usage:  %s [-v] program.bf\n", argv[0]);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int status = EXIT_SUCCESS;

	int opt = 0;
	bool verbose = false;

	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch (opt) {
		case 'v':
			verbose = true;
			break;
		default:
			usage(argv);
		}
	}

	/* missing mandatory file arg */
	if (optind >= argc)
		usage(argv);

	/* parse input */
	char *buffer = NULL;
	size_t len = 0;
	FILE *fp = fopen(argv[optind], "r");

	if (!fp) {
		fprintf(stderr, "%s does not exist\n", argv[optind]);
		exit(EXIT_FAILURE);
	}
	/* read into buffer */
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buffer = malloc(len + 1);
	if (!buffer) {
		perror("malloc");
		abort();
	}
	fread(buffer, 1, len, fp);
	buffer[len] = '\0';
	fclose(fp);

	/* llvm  jit thread context (we don't really make use of this feature
	 * though) */
	LLVMOrcThreadSafeContextRef tsctx = LLVMOrcCreateNewThreadSafeContext();
	LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(tsctx);

	LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("brain", ctx);

	/* lower to llvm ir */
	lower(buffer, mod, ctx, verbose);

	if (LLVMWriteBitcodeToFile(mod, "brain2llvm.bc")) {
		fprintf(stderr, "error writing bitcode to file\n");
		abort();
	}

	/* verify what we compiled */
	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMOrcThreadSafeModuleRef tsm = LLVMOrcCreateNewThreadSafeModule(
	    mod, tsctx);

	LLVMInitializeCore(LLVMGetGlobalPassRegistry());

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmPrinter();

	/* create jit instance */
	LLVMOrcLLJITRef lljit;
	LLVMErrorRef err;

	if ((err = LLVMOrcCreateLLJIT(&lljit, 0))) {
		status = handle_error(err);
		goto orc_llvm_fail;
	}

	/* Configure host symbol lookup. We don't filter any symbols. */
	LLVMOrcJITDylibDefinitionGeneratorRef sym_generator = 0;
	if ((err = LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(
		 &sym_generator, LLVMOrcLLJITGetGlobalPrefix(lljit), NULL,
		 NULL))) {
		status = handle_error(err);
		goto jit_cleanup;
	}

	LLVMOrcJITDylibAddGenerator(
	    LLVMOrcLLJITGetMainJITDylib(lljit), sym_generator);

	/* add module to jit instance */
	LLVMOrcJITDylibRef mainjd = LLVMOrcLLJITGetMainJITDylib(lljit);
	if ((err = LLVMOrcLLJITAddLLVMIRModule(lljit, mainjd, tsm))) {
		LLVMOrcDisposeThreadSafeModule(tsm);
		status = handle_error(err);
		goto module_add_fail;
	}

	/* look up address of jitted function */
	LLVMOrcJITTargetAddress jitted_addr;
	if ((err = LLVMOrcLLJITLookup(lljit, &jitted_addr, "jitted"))) {
		status = handle_error(err);
		goto jitted_addr_fail;
	}

	void (*jitted_ptr)(void) = (void (*)(void))jitted_addr;

	jitted_ptr();

module_add_fail:
jitted_addr_fail:
jit_cleanup:
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
