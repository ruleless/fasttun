phonyt?=allt

CC=gcc
CXX=g++
MAKE=make

CFLAGS+=-g -Wall
CXXFLAGS+=-g -Wall

AR=ar cr
RM=-rm -rf

# dirs:=$(shell find . -maxdepth 1 -type d)
# dirs:=$(basename $(patsubst ./%,%,$(dirs)))
# dirs:=$(filter-out $(exclude_dirs),$(dirs))
SUBDIRS:=$(dirs)

SRCS=$(wildcard *.cpp)
OBJS=$(SRCS:%.cpp=%.o)
DEPENDS=$(SRCS:%.cpp=%.d)


# Phony Target
.PHONY:allt objt fake
default:$(phonyt)

allt:$(EXEPATH) $(LIBPATH) $(TARGET) $(LIB) subdirs
objt:$(OBJS) subdirs

$(EXEPATH):
	mkdir $(EXEPATH)
$(LIBPATH):
	mkdir $(LIBPATH)


# First Layer
$(TARGET):$(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)
ifdef EXEPATH
	cp $@ $(EXEPATH)
endif

$(LIB):$(OBJS)
	$(AR)  $@  $^
ifdef LIBPATH
	cp $@ $(LIBPATH)
endif

subdirs:$(SUBDIRS)
	for dir in $(SUBDIRS); \
		do $(MAKE) -C $$dir||exit 1; \
	done


# Second Layer for object file deprule
$(OBJS):%.o:%.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

## Second Layer for dependency file deprule
-include $(DEPENDS)

$(DEPENDS):%.d:%.cpp
	set -e; rm -f $@; \
	$(CXX) -MM $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[:]*,\1.o $@:,g' < $@.$$$$ > $@; \
	rm $@.$$$$


# clean
clean:
	for dir in $(SUBDIRS);\
		do $(MAKE) -C $$dir clean||exit 1;\
	done
	$(RM) $(TARGET) $(LIB)  $(OBJS) $(DEPENDS)


# for debug
fake:
	@echo $(OBJS)
