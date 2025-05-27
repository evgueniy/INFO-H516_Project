# Makefile for building the decoder executable

# Compiler
CXX = g++

# Compiler flags: -Wall enables all warnings, -std=c++11 for C++11 standard,
CXXFLAGS = -g -Wall -std=c++11 -I. -I../encoder

# List of sources: decode.cpp is your main file in the decoder directory,
# ICSP_Codec_Decoder_source.cpp is the helper source file,
# and ../encoder/cabac.c is the C source file from the encoder directory.
SOURCES = decode.cpp ICSP_Codec_Decoder_source.cpp ../encoder/cabac.c

# The name of the final executable
EXECUTABLE = decode

# The default target
all: $(EXECUTABLE)

# Link step: compile all sources to create the executable.
$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(EXECUTABLE) $(SOURCES)

# Clean target to remove the executable
clean:
	rm -f $(EXECUTABLE)
