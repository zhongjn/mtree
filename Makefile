EXTENSION = mtree
MODULE_big = mtree
DATA = mtree--0.0.1.sql
OBJS = \
	mtreeentry.o \
	mtree.o \
	mtreebuild.o \
	mtreebuildbuffers.o \
	mtreeget.o \
	mtreeproc.o \
	mtreescan.o \
	mtreesplit.o \
	mtreeutil.o \
	mtreevacuum.o \
	mtreevalidate.o \
	mtreexlog.o

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)