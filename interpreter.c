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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "interpreter.h"
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
interpret(char *prog, bool trace)
{
	int tape[TAPE_SZ] = { 0 };
	int nesting = 0; /* nesting counter */
	int head = 0;	 /* tape pointer */

	char *const beg = prog;

	while (*prog) {

		if (trace)
			printf("bf: pc=%p head=%d, executing '%c'\n", prog,
			    head, *prog);

		switch (*prog) {
		case ',':
			tape[head] = getchar();
			prog++;
			break;
		case '.':
			putchar(tape[head]);
			prog++;
			break;
		case '-':
			--tape[head];
			prog++;
			break;
		case '+':
			++tape[head];
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
			if (head >= TAPE_SZ) {
				fprintf(stderr, "bf: tape overflow\n");
				abort();
			}
			prog++;
			break;
		case '[':
			if (tape[head]) {
				prog++;
				break;
			}

			/* jump after matching ] */
			nesting = 1;
			while (nesting && *++prog) {
				if (*prog == ']')
					nesting--;
				else if (*prog == '[')
					nesting++;
			}
			/* we should now be at the matching bracket */
			if (nesting) {
				fprintf(stderr, "bf: unmatched '['\n");
				abort();
			}
			prog++;
			break;
		case ']':
			if (!tape[head]) {
				prog++;
				break;
			}
			/* jump (backwards) to matching [ */
			nesting = 1;
			while (nesting && --prog >= beg) {
				if (*prog == '[')
					nesting--;
				else if (*prog == ']')
					nesting++;
			}
			if (nesting) {
				fprintf(stderr, "bf: unmatched ']'\n");
				abort();
			}
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
	if (trace)
		puts("bf: interpreter done");
}
