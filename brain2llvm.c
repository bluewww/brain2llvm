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

int
sum(int x, int y)
{
	return x + y;
}

/*
 * The BrainF language has 8 commands:
 * Command   Equivalent C    Action
 * -------   ------------    ------
 * ,         *h=getchar();   Read a character from stdin, 255 on EOF
 * .         putchar(*h);    Write a character to stdout
 * -         --*h;           Decrement tape
 * +         ++*h;           Increment tape
 * <         --h;            Move head left
 * >         ++h;            Move head right
 * [         while(*h) {     Start loop
 * ]         }               End loop
 */

#define TAPE_SZ (64 * 1024)

void
bf_interpret(char *prog, bool trace)
{
	int tape[TAPE_SZ] = { 0 };
	int *head;
	char *const beg = prog;
	head = tape;

	while (*prog) {

		if (trace)
			printf("bf: pc=%p executing '%c'\n", prog, *prog);

		switch (*prog) {
		case ',':
			*head = getchar();
			prog++;
			break;
		case '.':
			putchar(*head);
			prog++;
			break;
		case '-':
			--*head;
			prog++;
			break;
		case '+':
			++*head;
			prog++;
			break;
		case '<':
			if (head == 0) {
				fprintf(stderr, "bf: tape underflow\n");
				abort();
			}
			--head;
			prog++;
			break;
		case '>':
			++head;
			if (head >= tape + TAPE_SZ) {
				fprintf(stderr, "bf: tape overflow\n");
				abort();
			}
			prog++;
			break;
		case '[':
			if (*head) {
				prog++;
				break;
			}
			/* jump after ] */
			while (*prog != ']') {
				if (!*prog) {
					fprintf(stderr, "bf: pc overflow\n");
					abort();
				}
				prog++;
			}
			prog++;
			break;
		case ']':
			if (!(*head)) {
				prog++;
				break;
			}
			/* jump to [ */
			while (*prog != '[') {
				if (prog == beg) {
					fprintf(stderr, "bf: pc underflow\n");
					abort();
				}
				prog--;
			}
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
	if (trace)
		puts("bf: interpreter done");
}

int
main(int argc, char **argv)
{
	/* trivial loop */
	bf_interpret("[-]", true);

	/* hello world */
	bf_interpret(
	    ">++++++++[<+++++++++>-]<.>++++[<+++++++>-]<+.+++++++..+++.>>++++++[<+++++++>-]<++.------------.>++++++[<+++++++++>-]<+.<.+++.------.--------.>>>++++[<++++++++>-]<+.",
	    false);

	FILE *fp = fopen("mandelbrot.bf", "r");
	char *buffer = NULL;
	size_t len = 0;

	if (fp == NULL) {
		fprintf(stderr, "mandelbrot.bf does not exist, skipping\n");
	} else {
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
		/* run */
		bf_interpret(buffer, false);
	}

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
