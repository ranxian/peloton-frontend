## Makefile.am -- Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS	 = -I m4

# subdirectories of the project
SUBDIRS = \
		  src

# add files in the distribution
EXTRA_DIST = bootstrap $(DX_CONFIG) doc

clean-local:
	find . -type f -name "*.gcno" -delete; find . -type f -name "*.gcda" -delete
