BOCA_PATH = ..

include $(dir $(firstword $(MAKEFILE_LIST)))/$(BOCA_PATH)/Makefile-options

TARGETS	= picturedata$(EXECUTABLE) multivalue$(EXECUTABLE)

LIBS	= -lboca -lsmooth

CCOPTS	= -I $(BOCA_PATH)/include
LDOPTS	= -L $(BOCA_PATH)/$(LIBDIR) $(LIBS)

all: $(TARGETS)

picturedata$(EXECUTABLE): picturedata.o
	$(LD) -o $@ picturedata.o $(LDOPTS) $(LDFLAGS)

multivalue$(EXECUTABLE): multivalue.o
	$(LD) -o $@ multivalue.o $(LDOPTS) $(LDFLAGS)

check: multivalue$(EXECUTABLE)
	./multivalue$(EXECUTABLE)

clean:
	rm -f $(TARGETS) picturedata.o multivalue.o

.cpp.o:
	$(CXX) $(CCOPTS) $(CXXFLAGS) -c $< -o $@
