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

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interpreter.h"

int
main(void)
{
	/* trivial loop */
	interpret("[-]", true);

	/* hello world */
	interpret(
	    ">++++++++[<+++++++++>-]<.>++++[<+++++++>-]<+.+++++++..+++.>>++++++[<+++++++>-]<++.------------.>++++++[<+++++++++>-]<+.<.+++.------.--------.>>>++++[<++++++++>-]<+.",
	    false);
	puts("");

	/* count to five */
	interpret(
	    "++++++++ ++++++++ ++++++++ ++++++++ ++++++++ ++++++++ >+++++ [<+.>-]",
	    false);
	puts("");

	/* mandelbrot */
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
		interpret(buffer, false);
	}
	return EXIT_SUCCESS;
}
