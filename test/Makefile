BOCA_PATH = ..

include $(dir $(firstword $(MAKEFILE_LIST)))/$(BOCA_PATH)/Makefile-options

TARGETS	= picturedata$(EXECUTABLE) multivalue$(EXECUTABLE) discmetadata$(EXECUTABLE) cddb$(EXECUTABLE)
PLUGINS	= fakemetadata$(SHARED) minimalmetadata$(SHARED)

LIBS	= -lboca -lsmooth

CCOPTS	= -I $(BOCA_PATH)/include
LDOPTS	= -L $(BOCA_PATH)/$(LIBDIR) $(LIBS)

CDDB_PATH	= $(BOCA_PATH)/components/metadata/cddb
CDDB_OBJECTS	= cddb_cddb.o cddb_cddbbatch.o cddb_cddbcache.o cddb_cddbconfig.o cddb_cddbinfo.o cddb_cddblocal.o cddb_cddbremote.o cddb_convert.o

ifeq ($(BUILD_OSX),True)
	SHAREDOPTS = -dynamiclib
else
	SHAREDOPTS = --shared
endif

all: $(TARGETS) $(PLUGINS)

picturedata$(EXECUTABLE): picturedata.o
	$(LD) -o $@ picturedata.o $(LDOPTS) $(LDFLAGS)

multivalue$(EXECUTABLE): multivalue.o
	$(LD) -o $@ multivalue.o $(LDOPTS) $(LDFLAGS)

discmetadata$(EXECUTABLE): discmetadata.o $(PLUGINS)
	$(LD) -o $@ discmetadata.o $(LDOPTS) $(LDFLAGS)

cddb$(EXECUTABLE): cddb.o $(CDDB_OBJECTS)
	$(LD) -o $@ cddb.o $(CDDB_OBJECTS) $(LDOPTS) $(LDFLAGS)

cddb_%.o: $(CDDB_PATH)/%.cpp
	$(CXX) $(CCOPTS) $(CXXFLAGS) -c $< -o $@

%$(SHARED): %.cpp
	$(CXX) $(CCOPTS) -fPIC -fvisibility=hidden -DBOCA_COMPONENT_BUILD $(CXXFLAGS) -c $< -o $*.pic.o
	$(LD) $(SHAREDOPTS) -o $@ $*.pic.o $(LDOPTS) $(LDFLAGS)

# On Windows, BoCA loads components only from the boca folder next to
# the program, so check copies the tagger and metadata components there.
ifeq ($(BUILD_WIN32),True)
CHECKCOMPONENTS = components
endif

check: $(CHECKCOMPONENTS) multivalue$(EXECUTABLE) discmetadata$(EXECUTABLE) cddb$(EXECUTABLE)
	./multivalue$(EXECUTABLE)
	./discmetadata$(EXECUTABLE)
	./cddb$(EXECUTABLE)

components:
	mkdir -p boca
	cp $(BOCA_PATH)/$(BINDIR)/boca_tagger_*$(SHARED) $(BOCA_PATH)/$(BINDIR)/boca_metadata_*$(SHARED) boca

clean:
	rm -f $(TARGETS) $(PLUGINS) *.o
	rm -rf boca

.PHONY: check components clean

.cpp.o:
	$(CXX) $(CCOPTS) $(CXXFLAGS) -c $< -o $@
