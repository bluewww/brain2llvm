# Copyright 2021 ETH Zurich
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
# Author: Robert Balas (balasr@iis.ee.ethz.ch)


CC = clang
CXX = clang++

CFLAGS = -O2 -g3 `llvm-config --cflags` -Wall -Wextra
CXXFLAGS = -O2 -g3 `llvm-config --cxxflags` -Wall -Wextra

CPPFLAGS =

LDFLAGS = `llvm-config --ldflags`
LDLIBS = `llvm-config --libs core executionengine mcjit orcjit interpreter \
	analysis native bitwriter --system-libs`

all: brain2llvm tests

# for linking we need to use the c++ linker
brain2llvm: brain2llvm.o
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

tests: tests.o bf_interpreter.o
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

.PHONY: TAGS
test: tests
	./tests

# Note: --kinds-c=+p generates tag entries for header file prototypes (e.g. when
# the implementation is not available for example in compiled libraries)
.PHONY: TAGS
TAGS:
	ctags -R -e -h=".c.h" --kinds-c=+p ~/.local/include/ .

.PHONY: clean
clean:
	$(RM) brain2llvm tests *.o *.ll *.bc
