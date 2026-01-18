set(KRKRSDL_PLUGINS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

set(KRKRSDL_PLUGINS_SRC
    ${KRKRSDL_PLUGINS_PATH}/ncbind/ncbind.cpp
    ${KRKRSDL_PLUGINS_PATH}/addFont.cpp
	${KRKRSDL_PLUGINS_PATH}/AlphaMovie.cpp
	${KRKRSDL_PLUGINS_PATH}/csvParser.cpp
	${KRKRSDL_PLUGINS_PATH}/dirlist.cpp
	${KRKRSDL_PLUGINS_PATH}/DrawDeviceD3D.cpp
	${KRKRSDL_PLUGINS_PATH}/expat.cpp
	${KRKRSDL_PLUGINS_PATH}/Extension.cpp
	${KRKRSDL_PLUGINS_PATH}/fftgraph.cpp
	${KRKRSDL_PLUGINS_PATH}/fstat.cpp
	${KRKRSDL_PLUGINS_PATH}/FuncStubs.cpp
	${KRKRSDL_PLUGINS_PATH}/getabout.cpp
	${KRKRSDL_PLUGINS_PATH}/getSample.cpp
	${KRKRSDL_PLUGINS_PATH}/gfxEffect.cpp
	${KRKRSDL_PLUGINS_PATH}/gfxFire.cpp
	${KRKRSDL_PLUGINS_PATH}/json.cpp
	${KRKRSDL_PLUGINS_PATH}/kirikiroid2.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExAreaAverage.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExBase.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExBTOA.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExImage.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExMovie.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExPerspective.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExRaster.cpp
	${KRKRSDL_PLUGINS_PATH}/LayerExSave.cpp
	${KRKRSDL_PLUGINS_PATH}/motionPlayer.cpp
	${KRKRSDL_PLUGINS_PATH}/PackinOne.cpp
	${KRKRSDL_PLUGINS_PATH}/saveStruct.cpp
	${KRKRSDL_PLUGINS_PATH}/scriptsEx.cpp
	${KRKRSDL_PLUGINS_PATH}/shrinkCopy.cpp
	${KRKRSDL_PLUGINS_PATH}/textrender.cpp
	${KRKRSDL_PLUGINS_PATH}/TVPPlugin.cpp
	${KRKRSDL_PLUGINS_PATH}/varfile.cpp
	${KRKRSDL_PLUGINS_PATH}/win32dialog.cpp
	${KRKRSDL_PLUGINS_PATH}/windowEx.cpp
	${KRKRSDL_PLUGINS_PATH}/wutcwf.cpp
	${KRKRSDL_PLUGINS_PATH}/xp3filter.cpp
	${KRKRSDL_PLUGINS_PATH}/emoteplayer/emotefile.cpp
	${KRKRSDL_PLUGINS_PATH}/emoteplayer/emoteplayer.cpp
	${KRKRSDL_PLUGINS_PATH}/emoteplayer/emoteplayerclass.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/extrans.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/mosaic.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/ripple.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/rotatebase.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/rotatetrans.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/turn.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/turntrans_table.cpp
	${KRKRSDL_PLUGINS_PATH}/extrans/wave.cpp
	${KRKRSDL_PLUGINS_PATH}/KAGParserEx/KAG.cpp
	${KRKRSDL_PLUGINS_PATH}/KAGParserEx/KAGParserEx.cpp
	${KRKRSDL_PLUGINS_PATH}/psbfile/PSB.cpp
	${KRKRSDL_PLUGINS_PATH}/psbfile/PSBData.cpp
	${KRKRSDL_PLUGINS_PATH}/psbfile/PSBFile.cpp
	${KRKRSDL_PLUGINS_PATH}/psbfile/PSBMedia.cpp
    )
	
if(ANDROID OR LINUX)
    list(APPEND KRKRSDL_PLUGINS_SRC ${KRKRSDL_PLUGINS_PATH}/LayerExDraw/general/LayerExDraw.cpp)
elseif(WINDOWS)
    list(APPEND KRKRSDL_PLUGINS_SRC ${KRKRSDL_PLUGINS_PATH}/LayerExDraw/windows/LayerExDraw.cpp)
else()
    message(FATAL_ERROR "unsupported: ${CMAKE_SYSTEM_NAME}")
endif()
