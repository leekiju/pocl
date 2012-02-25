/* Tests a kernel with WI id dependent computation just before function exit.

   Copyright (c) 2012 Pekka Jääskeläinen / Tampere University of Technology
   
   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

// Enable OpenCL C++ exceptions
#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#define WINDOW_SIZE 32
#define WORK_ITEMS 32
#define BUFFER_SIZE (WORK_ITEMS + WINDOW_SIZE)

/* This is an interesting case because of the
   barrier "all WIs or none" semantics.
   This is a case of a conditional barrier.
   
   The first if, due to the barrier, can be
   assumed to be "all WIs or none" because
   if it's not, the kernel execution becomes
   undefined as the barrier region is entered
   by only a subset of the WIs. 

   The second if should be replicated normally
   by chaining. The problem here seems to be that
   the else branches to the exit so the PR formation
   gets confused.
*/

static char
kernelSourceCode[] = 
"kernel \n"
"void test_kernel(__global float *input, \n"
"                 __global int *result) {\n"
" size_t gid = get_global_id(0);\n"
" if (input[0] > 2) return;\n"
" result[gid] = 0;\n"
" barrier(CLK_GLOBAL_MEM_FENCE);\n"
" if (gid == 1) {\n"
"    result[gid] = 43;\n"
" } else \n"
"    result[gid] += input[gid];\n"
" barrier(CLK_GLOBAL_MEM_FENCE);\n"
"}\n";

int
main(void)
{
    float A[BUFFER_SIZE];
    int R[WORK_ITEMS];
    cl_int err;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        A[i] = i;
    }

    for (int i = 0; i < WORK_ITEMS; i++) {
        R[i] = i;
    }

    try {
        std::vector<cl::Platform> platformList;

        // Pick platform
        cl::Platform::get(&platformList);

        // Pick first platform
        cl_context_properties cprops[] = {
            CL_CONTEXT_PLATFORM, (cl_context_properties)(platformList[0])(), 0};
        cl::Context context(CL_DEVICE_TYPE_CPU, cprops);

        // Query the set of devices attched to the context
        std::vector<cl::Device> devices = context.getInfo<CL_CONTEXT_DEVICES>();

        // Create and program from source
        cl::Program::Sources sources(1, std::make_pair(kernelSourceCode, 0));
        cl::Program program(context, sources);

        // Build program
        program.build(devices);

        // Create buffer for A and copy host contents
        cl::Buffer aBuffer = cl::Buffer(
            context, 
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
            BUFFER_SIZE * sizeof(float), 
            (void *) &A[0]);

        // Create buffer for that uses the host ptr C
        cl::Buffer cBuffer = cl::Buffer(
            context, 
            CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR, 
            WORK_ITEMS * sizeof(int), 
            (void *) &R[0]);

        // Create kernel object
        cl::Kernel kernel(program, "test_kernel");

        // Set kernel args
        kernel.setArg(0, aBuffer);
        kernel.setArg(1, cBuffer);

        // Create command queue
        cl::CommandQueue queue(context, devices[0], 0);
 
        // Do the work
        queue.enqueueNDRangeKernel(
            kernel, 
            cl::NullRange, 
            cl::NDRange(WORK_ITEMS),
            cl::NullRange);
 

        // Map cBuffer to host pointer. This enforces a sync with 
        // the host backing space, remember we choose GPU device.
        int * output = (int *) queue.enqueueMapBuffer(
            cBuffer,
            CL_TRUE, // block 
            CL_MAP_READ,
            0,
            WORK_ITEMS * sizeof(int));

        bool ok = true;
        for (int i = 0; i < WORK_ITEMS; i++) {
            int correct = i;
            if (i == 1)
                correct = 43;
            else 
                correct = i;
            if ((int)R[i] != correct) {
                std::cout 
                    << "F(" << i << ": " << R[i] << " != " << correct 
                    << ") ";
                ok = false;
            }
        }
        if (ok) std::cout << "OK" << std::endl;

        // Finally release our hold on accessing the memory
        err = queue.enqueueUnmapMemObject(
            cBuffer,
            (void *) output);
 
        // There is no need to perform a finish on the final unmap
        // or release any objects as this all happens implicitly with
        // the C++ Wrapper API.
    } 
    catch (cl::Error err) {
         std::cerr
             << "ERROR: "
             << err.what()
             << "("
             << err.err()
             << ")"
             << std::endl;

         return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
