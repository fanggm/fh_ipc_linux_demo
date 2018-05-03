include Rules.make

SUBDIRS = $(foreach d,$(shell find . -maxdepth 2 -mindepth 2 -name "Makefile" | xargs -i dirname {}),$(patsubst ./%,%,$(d)))

CLEANSUBDIRS = $(addsuffix .clean, $(SUBDIRS))

.PHONY: $(SUBDIRS) $(CLEANSUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	@$(MAKE) -C $@

clean: $(CLEANSUBDIRS)
	find . -name "*.o" | xargs rm -f
	find . -name "*.d" | xargs rm -f

$(CLEANSUBDIRS):
	@cd $(basename $@) ; $(MAKE) clean
