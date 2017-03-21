/* -*- c++ -*- */
/* 
 * Copyright 2017 ghostop14.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "clComplexToArg_impl.h"
#include "fast_atan2f.h"

namespace gr {
  namespace clenabled {

    clComplexToArg::sptr
    clComplexToArg::make(int openCLPlatformType, int devSelector,int platformId, int devId,int setDebug)
    {
    	if (setDebug == 1)
		  return gnuradio::get_initial_sptr
			(new clComplexToArg_impl(openCLPlatformType,devSelector,platformId,devId,true));
    	else
  		  return gnuradio::get_initial_sptr
  			(new clComplexToArg_impl(openCLPlatformType,devSelector,platformId,devId,false));
    }

    /*
     * The private constructor
     */
    clComplexToArg_impl::clComplexToArg_impl(int openCLPlatformType, int devSelector,int platformId, int devId,bool setDebug)
      : gr::block("clComplexToArg",
              gr::io_signature::make(1, 1, sizeof(gr_complex)),
              gr::io_signature::make(1, 1, sizeof(float))),
	  GRCLBase(DTYPE_COMPLEX, sizeof(gr_complex),openCLPlatformType,devSelector,platformId,devId,setDebug)
{
    	int imaxItems=gr::block::max_noutput_items();
    	if (imaxItems==0)
    		imaxItems=8192;
/*
    	if (imaxItems > maxConstItems) {
    		imaxItems = maxConstItems;
    	}

		try {
			// optimize for constant memory space
			gr::block::set_max_noutput_items(imaxItems);
		}
		catch(...) {

		}
*/
		setBufferLength(imaxItems);

        // And finally optimize the data we get based on the preferred workgroup size.
        // Note: We can't do this until the kernel is compiled and since it's in the block class
        // it has to be done here.
        // Note: for CPU's adjusting the workgroup size away from 1 seems to decrease performance.
        // For GPU's setting it to the preferred size seems to have the best performance.
		try {
			if (contextType != CL_DEVICE_TYPE_CPU) {
				gr::block::set_output_multiple(preferredWorkGroupSizeMultiple);
			}
		}
        catch (...) {

        }
}
    clComplexToArg_impl::~clComplexToArg_impl()
    {
    	if (curBufferSize > 0)
    		stop();
    }

    bool clComplexToArg_impl::stop() {
    	curBufferSize = 0;

    	if (aBuffer) {
    		delete aBuffer;
    		aBuffer = NULL;
    	}

    	if (cBuffer) {
    		delete cBuffer;
    		cBuffer = NULL;
    	}

    	return GRCLBase::stop();
    }

    void clComplexToArg_impl::buildKernel(int numItems) {
    	maxConstItems = (int)((float)maxConstMemSize / ((float)dataSize));
    	bool useConst;

    	if (numItems > maxConstItems)
    		useConst = false;
    	else
    		useConst = true;

		if (debugMode) {
			if (useConst)
				std::cout << "OpenCL INFO: MComplexToArg building kernel with __constant params..." << std::endl;
			else
				std::cout << "OpenCL INFO: ComplexToArg - too many items for constant memory.  Building kernel with __global params..." << std::endl;
		}

    	// Now we set up our OpenCL kernel
        std::string srcStdStr="";
        std::string fnName = "complextoarg";

    	srcStdStr += "struct ComplexStruct {\n";
    	srcStdStr += "float real;\n";
    	srcStdStr += "float imag; };\n";
    	srcStdStr += "typedef struct ComplexStruct SComplex;\n";
    	if (useConst)
    		srcStdStr += "__kernel void complextoarg(__constant SComplex * a, __global float * restrict c) {\n";
    	else
    		srcStdStr += "__kernel void complextoarg(__global SComplex * restrict a, __global float * restrict c) {\n";

    	srcStdStr += "    size_t index =  get_global_id(0);\n";
    	srcStdStr += "    c[index] = atan2(a[index].imag,a[index].real);\n";
    	srcStdStr += "}\n";

    	int imaxItems=gr::block::max_noutput_items();
    	if (imaxItems==0)
    		imaxItems=8192;

    	maxConstItems = (int)((float)maxConstMemSize / ((float)sizeof(gr_complex)));

    	if (maxConstItems < imaxItems || imaxItems == 0) {
    		gr::block::set_max_noutput_items(maxConstItems);
    		imaxItems = maxConstItems;

    		if (debugMode)
    			std::cout << "OpenCL INFO: ComplexToArg adjusting output buffer for " << maxConstItems << " due to OpenCL constant memory restrictions" << std::endl;
		}
		else {
			if (debugMode)
				std::cout << "OpenCL INFO: ComplexToArg using default output buffer of " << imaxItems << "..." << std::endl;
		}

        GRCLBase::CompileKernel((const char *)srcStdStr.c_str(),(const char *)fnName.c_str());
    }

    void clComplexToArg_impl::setBufferLength(int numItems) {
    	if (aBuffer)
    		delete aBuffer;

    	if (cBuffer)
    		delete cBuffer;

    	aBuffer = new cl::Buffer(
            *context,
            CL_MEM_READ_ONLY,
			numItems * sizeof(gr_complex));

        cBuffer = new cl::Buffer(
            *context,
            CL_MEM_READ_WRITE,
			numItems * sizeof(float));

        buildKernel(numItems);

        curBufferSize=numItems;
    }

    /*
     * Our virtual destructor.
     */
    int clComplexToArg_impl::testCPU(int noutput_items,
            gr_vector_int &ninput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items)
    	{
        gr_complex *in = (gr_complex*)input_items[0];
        float *out = (float*)output_items[0];

        for(int i = 0; i < noutput_items; i++) {
        	out[i] = fast_atan2f(in[i].imag(),in[i].real());
        }

        return noutput_items;
    }

    int clComplexToArg_impl::testOpenCL(int noutput_items,
            gr_vector_int &ninput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items) {
    	return processOpenCL(noutput_items,ninput_items,input_items, output_items);
    }

    int clComplexToArg_impl::processOpenCL(int noutput_items,
            gr_vector_int &ninput_items,
            gr_vector_const_void_star &input_items,
            gr_vector_void_star &output_items)
    {

		if (kernel == NULL) {
			return 0;
		}

    	if (noutput_items > curBufferSize) {
    		setBufferLength(noutput_items);
    	}

    	int inputSize = noutput_items*sizeof(gr_complex);
        queue->enqueueWriteBuffer(*aBuffer,CL_TRUE,0,inputSize,input_items[0]);

		// Do the work

		// Set kernel args
		kernel->setArg(0, *aBuffer);
		kernel->setArg(1, *cBuffer);

		cl::NDRange localWGSize=cl::NullRange;

		if (contextType!=CL_DEVICE_TYPE_CPU) {
			if (noutput_items % preferredWorkGroupSizeMultiple == 0 && (noutput_items < 8192)) {
				// for some reason problems start to happen when we're no longer using constant memory
				localWGSize=cl::NDRange(preferredWorkGroupSizeMultiple);
			}
		}

		// Do the work
		queue->enqueueNDRangeKernel(
			*kernel,
			cl::NullRange,
			cl::NDRange(noutput_items),
			localWGSize);


    // Map cBuffer to host pointer. This enforces a sync with
    // the host

	queue->enqueueReadBuffer(*cBuffer,CL_TRUE,0,noutput_items*sizeof(float),(void *)output_items[0]);

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

    void
    clComplexToArg_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
      /* <+forecast+> e.g. ninput_items_required[0] = noutput_items */
    }

    int
    clComplexToArg_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
        int retVal = processOpenCL(noutput_items,ninput_items,input_items,output_items);

      consume_each (noutput_items);

      // Tell runtime system how many output items we produced.
      return retVal;
    }

  } /* namespace clenabled */
} /* namespace gr */

