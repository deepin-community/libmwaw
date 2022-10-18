noinst_PROGRAMS = \
	actafuzzer powerpointfuzzer mswrdfuzzer mswksfuzzer \
	beaglewksfuzzer greatwksfuzzer clariswksfuzzer applepictfuzzer \
        clarisdrawfuzzer cricketdrawfuzzer freehandfuzzer macdraftfuzzer \
	macdrawfuzzer macpaintfuzzer pixelpaintfuzzer superpaintfuzzer \
	wingzfuzzer docmkrfuzzer edocfuzzer fullwrtfuzzer hanmacwrdfuzzer \
	lightwaytxtfuzzer macdocfuzzer macwrtfuzzer marinerwrtfuzzer \
	maxwrtfuzzer mindwrtfuzzer morefuzzer mousewrtfuzzer nisuswrtfuzzer \
	ragtimefuzzer stylefuzzer teachtxtfuzzer writenowfuzzer \
	writerplsfuzzer zwrtfuzzer multiplanfuzzer

AM_CXXFLAGS = -I$(top_srcdir)/inc \
	$(REVENGE_GENERATORS_CFLAGS) \
	$(REVENGE_STREAM_CFLAGS) \
	$(REVENGE_CFLAGS) \
	$(DEBUG_CXXFLAGS)

commonfuzzer_ldadd = \
	$(top_builddir)/src/lib/libmwaw-@MWAW_MAJOR_VERSION@.@MWAW_MINOR_VERSION@.la \
	$(REVENGE_GENERATORS_LIBS) \
	$(REVENGE_STREAM_LIBS) \
	$(REVENGE_LIBS) \
	-lFuzzingEngine

actafuzzer_LDADD = $(commonfuzzer_ldadd)

actafuzzer_SOURCES = \
	actafuzzer.cpp

powerpointfuzzer_LDADD = $(commonfuzzer_ldadd)

powerpointfuzzer_SOURCES = \
	powerpointfuzzer.cpp

mswrdfuzzer_LDADD = $(commonfuzzer_ldadd)

mswrdfuzzer_SOURCES = \
	mswrdfuzzer.cpp

mswksfuzzer_LDADD = $(commonfuzzer_ldadd)

mswksfuzzer_SOURCES = \
	mswksfuzzer.cpp

beaglewksfuzzer_LDADD = $(commonfuzzer_ldadd)

beaglewksfuzzer_SOURCES = \
	beaglewksfuzzer.cpp

greatwksfuzzer_LDADD = $(commonfuzzer_ldadd)

greatwksfuzzer_SOURCES = \
	greatwksfuzzer.cpp

clariswksfuzzer_LDADD = $(commonfuzzer_ldadd)

clariswksfuzzer_SOURCES = \
	clariswksfuzzer.cpp

applepictfuzzer_LDADD = $(commonfuzzer_ldadd)

applepictfuzzer_SOURCES = \
	applepictfuzzer.cpp

clarisdrawfuzzer_LDADD = $(commonfuzzer_ldadd)

clarisdrawfuzzer_SOURCES = \
	clarisdrawfuzzer.cpp

cricketdrawfuzzer_LDADD = $(commonfuzzer_ldadd)

cricketdrawfuzzer_SOURCES = \
	cricketdrawfuzzer.cpp

freehandfuzzer_LDADD = $(commonfuzzer_ldadd)

freehandfuzzer_SOURCES = \
	freehandfuzzer.cpp

macdraftfuzzer_LDADD = $(commonfuzzer_ldadd)

macdraftfuzzer_SOURCES = \
	macdraftfuzzer.cpp

macdrawfuzzer_LDADD = $(commonfuzzer_ldadd)

macdrawfuzzer_SOURCES = \
	macdrawfuzzer.cpp

macpaintfuzzer_LDADD = $(commonfuzzer_ldadd)

macpaintfuzzer_SOURCES = \
	macpaintfuzzer.cpp

pixelpaintfuzzer_LDADD = $(commonfuzzer_ldadd)

pixelpaintfuzzer_SOURCES = \
	pixelpaintfuzzer.cpp

superpaintfuzzer_LDADD = $(commonfuzzer_ldadd)

superpaintfuzzer_SOURCES = \
	superpaintfuzzer.cpp

wingzfuzzer_LDADD = $(commonfuzzer_ldadd)

wingzfuzzer_SOURCES = \
	wingzfuzzer.cpp

docmkrfuzzer_LDADD = $(commonfuzzer_ldadd)

docmkrfuzzer_SOURCES = \
	docmkrfuzzer.cpp

edocfuzzer_LDADD = $(commonfuzzer_ldadd)

edocfuzzer_SOURCES = \
	edocfuzzer.cpp

fullwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

fullwrtfuzzer_SOURCES = \
	fullwrtfuzzer.cpp

hanmacwrdfuzzer_LDADD = $(commonfuzzer_ldadd)

hanmacwrdfuzzer_SOURCES = \
	hanmacwrdfuzzer.cpp

lightwaytxtfuzzer_LDADD = $(commonfuzzer_ldadd)

lightwaytxtfuzzer_SOURCES = \
	lightwaytxtfuzzer.cpp

macdocfuzzer_LDADD = $(commonfuzzer_ldadd)

macdocfuzzer_SOURCES = \
	macdocfuzzer.cpp

macwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

macwrtfuzzer_SOURCES = \
	macwrtfuzzer.cpp

marinerwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

marinerwrtfuzzer_SOURCES = \
	marinerwrtfuzzer.cpp

maxwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

maxwrtfuzzer_SOURCES = \
	maxwrtfuzzer.cpp

mindwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

mindwrtfuzzer_SOURCES = \
	mindwrtfuzzer.cpp

morefuzzer_LDADD = $(commonfuzzer_ldadd)

morefuzzer_SOURCES = \
	morefuzzer.cpp

mousewrtfuzzer_LDADD = $(commonfuzzer_ldadd)

mousewrtfuzzer_SOURCES = \
	mousewrtfuzzer.cpp

nisuswrtfuzzer_LDADD = $(commonfuzzer_ldadd)

nisuswrtfuzzer_SOURCES = \
	nisuswrtfuzzer.cpp

ragtimefuzzer_LDADD = $(commonfuzzer_ldadd)

ragtimefuzzer_SOURCES = \
	ragtimefuzzer.cpp

stylefuzzer_LDADD = $(commonfuzzer_ldadd)

stylefuzzer_SOURCES = \
	stylefuzzer.cpp

teachtxtfuzzer_LDADD = $(commonfuzzer_ldadd)

teachtxtfuzzer_SOURCES = \
	teachtxtfuzzer.cpp

writenowfuzzer_LDADD = $(commonfuzzer_ldadd)

writenowfuzzer_SOURCES = \
	writenowfuzzer.cpp

writerplsfuzzer_LDADD = $(commonfuzzer_ldadd)

writerplsfuzzer_SOURCES = \
	writerplsfuzzer.cpp

zwrtfuzzer_LDADD = $(commonfuzzer_ldadd)

zwrtfuzzer_SOURCES = \
	zwrtfuzzer.cpp

multiplanfuzzer_LDADD = $(commonfuzzer_ldadd)

multiplanfuzzer_SOURCES = \
	multiplanfuzzer.cpp
