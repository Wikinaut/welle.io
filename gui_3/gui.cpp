#
/*
 *    Copyright (C) 2013, 2014, 2015
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
 *
 *    This file is part of the  SDR-J (JSDR).
 *    Many of the ideas as implemented in SDR-J are derived from
 *    other work, made available through the GNU general Public License. 
 *    All copyrights of the original authors are acknowledged.
 *
 *    SDR-J is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    SDR-J is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with SDR-J; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include	<QSettings>
#include	"dab-constants.h"
#include	"gui.h"
#include	"audiosink.h"

#include	"fft.h"
#include	"rawfiles.h"
#include	"wavfiles.h"
#ifdef	HAVE_DABSTICK
#include	"dabstick.h"
#endif
#ifdef	HAVE_SDRPLAY
#include	"sdrplay.h"
#endif
#ifdef	HAVE_UHD
#include	"uhd-input.h"
#endif
#ifdef	HAVE_EXTIO
#include	"extio-handler.h"
#endif
#ifdef	HAVE_RTL_TCP
#include	"rtl_tcp_client.h"
#endif
#ifdef	HAVE_AIRSPY
#include	"airspy-handler.h"
#endif

#define		BAND_III	0100
#define		L_BAND		0101
/**
  *	We use the creation function merely to set up the
  *	user interface and make the connections between the
  *	gui elements and the handling agents. All real action
  *	is embedded in actions, initiated by gui buttons
  */
    RadioInterface::RadioInterface (QSettings	*Si, QQmlApplicationEngine *engine,
                                    QObject		*parent): QObject (parent) {
int16_t	latency;

	dabSettings		= Si;
    this->engine    = engine;
//
//	Before printing anything, we set
	setlocale (LC_ALL, "");
///	The default, most likely to be overruled
//
//	One can imagine that a particular version is created
//	for a specific device. In that case the class
//	handling the device can be instantiated here
	inputDevice		= new virtualInput ();
	running			= false;
	
/**	threshold is used in the phaseReference class 
  *	as threshold for checking the validity of the correlation result
  */
	threshold	=
               dabSettings -> value ("threshold", 3). toInt ();

	isSynced		= UNSYNCED;
//
//	latency is used to allow different settings for different
//	situations wrt the output buffering
	latency			=
	           dabSettings -> value ("latency", 1). toInt ();
/**
  *	The current setup of the audio output is that
  *	you have a choice, take one of (soundcard, tcp streamer or rtp streamer)
  */
	audioBuffer		= new RingBuffer<int16_t>(2 * 32768);
	ipAddress		= dabSettings -> value ("ipAddress", "127.0.0.1"). toString ();
	port			= dabSettings -> value ("port", 8888). toInt ();
//
//	show_crcErrors can be ignored in other GUI's, the
//	value is passed on though
	show_crcErrors		= dabSettings -> value ("show_crcErrors", 0). toInt () != 0;
	autoStart		= dabSettings -> value ("autoStart", 0). toInt () != 0;
//
//	In this version, the default is sending the resulting PCM samples to the
//	soundcard. However, defining either TCP_STREAMER or RTP_STREAMER will
//	cause the PCM samples to be send through a different medium
    QStringList AudioInterfaces;
    soundOut = new audioSink (latency, &AudioInterfaces, audioBuffer);
/**
  *	By default we select Band III and Mode 1 or whatever the use
  *	has specified
  */
	dabBand		= dabSettings	-> value ("dabBand", BAND_III). toInt ();
    //setupChannels	(channelSelector, dabBand);

	uint8_t dabMode	= dabSettings	-> value ("dabMode", 1). toInt ();
	setModeParameters (dabMode);
/**
  *	The actual work is done elsewhere: in ofdmProcessor
  *	and ofdmDecoder for the ofdm related part, ficHandler
  *	for the FIC's and mscHandler for the MSC.
  *	The ficHandler shares information with the mscHandler
  *	but the handlers do not change each others modes.
  */
	my_mscHandler		= new mscHandler	(this,
                                             &dabModeParameters,
                                             audioBuffer,
                                             show_crcErrors);
	my_ficHandler		= new ficHandler	(this);
//
/**
  *	The default for the ofdmProcessor depends on
  *	the input device, so changing the selection for an input device
  *	requires changing the ofdmProcessor.
  */
	my_ofdmProcessor = new ofdmProcessor   (inputDevice,
	                                        &dabModeParameters,
	                                        this,
	                                        my_mscHandler,
	                                        my_ficHandler,
	                                        threshold);
	init_your_gui ();		// gui specific stuff

    if (autoStart)
       setStart ();

    // Read channels from the settings
    dabSettings->beginGroup("channels");
    int channelcount = dabSettings->value("channelcout").toInt();

    for(int i=1;i<=channelcount;i++)
    {
        QStringList SaveChannel = dabSettings->value("channel/"+QString::number(i)).toStringList();
        stationList.append(SaveChannel.first(), SaveChannel.last());
    }
    dabSettings->endGroup();

    // Sort stations
    stationList.sort();

    // Set timer
    connect(&CheckFICTimer, SIGNAL(timeout()),this, SLOT(CheckFICTimerTimeout()));
    connect(&ScanChannelTimer, SIGNAL(timeout()),this, SLOT(scanChannelTimerTimeout()));

    // Reset
    isSignalPresent = false;
    isFICCRC = false;

    // Main entry to the QML GUI
    QQmlContext *rootContext = engine->rootContext();

    // Set the stations
    rootContext->setContextProperty("stationModel", QVariant::fromValue(stationList.getList()));
    rootContext->setContextProperty("cppGUI", this);

    // Set working directory
    QString workingDir = QDir::currentPath() + "/";
    rootContext->setContextProperty("workingDir", workingDir);

    // Take the root object
    QObject *rootObject = engine->rootObjects().first();

    // Set the full screen property
    bool isFullscreen = dabSettings	-> value ("StartInFullScreen", false).toBool();
    QObject *enableFullScreenObject = rootObject->findChild<QObject*>("enableFullScreen");
    if(enableFullScreenObject)
        enableFullScreenObject->setProperty("checked", isFullscreen);

    // Set the show channel names property
    bool isShowChannelNames = dabSettings-> value ("ShowChannelNames", false).toBool();
    QObject *showChannelObject = rootObject->findChild<QObject*>("showChannel");
    if(showChannelObject)
        showChannelObject->setProperty("checked", isShowChannelNames);

    // Connect signals
    connect(rootObject, SIGNAL(stationClicked(QString,QString)),this, SLOT(channelClick(QString,QString)));
    connect(rootObject, SIGNAL(startChannelScanClicked()),this, SLOT(startChannelScanClick()));
    connect(rootObject, SIGNAL(stopChannelScanClicked()),this, SLOT(stopChannelScanClick()));
    connect(rootObject, SIGNAL(exitApplicationClicked()),this, SLOT(TerminateProcess()));
    connect(rootObject, SIGNAL(exitSettingsClicked()),this, SLOT(saveSettings()));
}

	RadioInterface::~RadioInterface () {
	fprintf (stderr, "deleting radioInterface\n");
    //TerminateProcess();
}
//
/**
  *	\brief At the end, we might save some GUI values
  *	The QSettings could have been the class variable as well
  *	as the parameter
  */
void	RadioInterface::dumpControlState (QSettings *s) {
	if (s == NULL)	// cannot happen
	   return;

    s->setValue ("device", CurrentDevice);

    // Remove old channels
    s->beginGroup("channels");
    int ChannelCount = s->value("channelcout").toInt();

    for(int i=1;i<=ChannelCount;i++)
    {
        s->remove("channel/"+QString::number(i));
    }

    // Save channels
    ChannelCount = stationList.count();
    s->setValue("channelcout",QString::number(ChannelCount));

    for(int i=1;i<=ChannelCount;i++)
    {
        s->setValue("channel/"+QString::number(i), stationList.getStationAt(i-1));
    }
    dabSettings->endGroup();

    /* Read settings from GIU */

    // Take the root object
    QObject *rootObject = engine->rootObjects().first();

    // Access the full screen mode switch
    QObject *enableFullScreenObject = rootObject->findChild<QObject*>("enableFullScreen");
    if(enableFullScreenObject)
    {
        bool isFullScreen = enableFullScreenObject->property("checked").toBool();
        // Save the setting
        s->setValue("StartInFullScreen", isFullScreen);
    }

    // Access the visible channel names
    QObject *showChannelObject = rootObject->findChild<QObject*>("showChannel");
    if(showChannelObject)
    {
        bool isShowChannel = showChannelObject->property("checked").toBool();
        // Save the setting
        s->setValue("ShowChannelNames", isShowChannel);
    }
}
//
///	the values for the different Modes:
void	RadioInterface::setModeParameters (uint8_t Mode) {
	if (Mode == 2) {
	   dabModeParameters. dabMode	= 2;
	   dabModeParameters. L		= 76;		// blocks per frame
	   dabModeParameters. K		= 384;		// carriers
	   dabModeParameters. T_null	= 664;		// null length
	   dabModeParameters. T_F	= 49152;	// samples per frame
	   dabModeParameters. T_s	= 638;		// block length
	   dabModeParameters. T_u	= 512;		// useful part
	   dabModeParameters. guardLength	= 126;
	   dabModeParameters. carrierDiff	= 4000;
	} else
	if (Mode == 4) {
	   dabModeParameters. dabMode		= 4;
	   dabModeParameters. L			= 76;
	   dabModeParameters. K			= 768;
	   dabModeParameters. T_F		= 98304;
	   dabModeParameters. T_null		= 1328;
	   dabModeParameters. T_s		= 1276;
	   dabModeParameters. T_u		= 1024;
	   dabModeParameters. guardLength	= 252;
	   dabModeParameters. carrierDiff	= 2000;
	} else 
	if (Mode == 3) {
	   dabModeParameters. dabMode		= 3;
	   dabModeParameters. L			= 153;
	   dabModeParameters. K			= 192;
	   dabModeParameters. T_F		= 49152;
	   dabModeParameters. T_null		= 345;
	   dabModeParameters. T_s		= 319;
	   dabModeParameters. T_u		= 256;
	   dabModeParameters. guardLength	= 63;
	   dabModeParameters. carrierDiff	= 2000;
	} else {	// default = Mode I
	   dabModeParameters. dabMode		= 1;
	   dabModeParameters. L			= 76;
	   dabModeParameters. K			= 1536;
	   dabModeParameters. T_F		= 196608;
	   dabModeParameters. T_null		= 2656;
	   dabModeParameters. T_s		= 2552;
	   dabModeParameters. T_u		= 2048;
	   dabModeParameters. guardLength	= 504;
	   dabModeParameters. carrierDiff	= 1000;
	}
}

struct dabFrequencies {
	const char	*key;
	int	fKHz;
};

struct dabFrequencies bandIII_frequencies [] = {
{"5A",	174928},
{"5B",	176640},
{"5C",	178352},
{"5D",	180064},
{"6A",	181936},
{"6B",	183648},
{"6C",	185360},
{"6D",	187072},
{"7A",	188928},
{"7B",	190640},
{"7C",	192352},
{"7D",	194064},
{"8A",	195936},
{"8B",	197648},
{"8C",	199360},
{"8D",	201072},
{"9A",	202928},
{"9B",	204640},
{"9C",	206352},
{"9D",	208064},
{"10A",	209936},
{"10B", 211648},
{"10C", 213360},
{"10D", 215072},
{"11A", 216928},
{"11B",	218640},
{"11C",	220352},
{"11D",	222064},
{"12A",	223936},
{"12B",	225648},
{"12C",	227360},
{"12D",	229072},
{"13A",	230748},
{"13B",	232496},
{"13C",	234208},
{"13D",	235776},
{"13E",	237488},
{"13F",	239200},
{NULL, 0}
};

struct dabFrequencies Lband_frequencies [] = {
{"LA", 1452960},
{"LB", 1454672},
{"LC", 1456384},
{"LD", 1458096},
{"LE", 1459808},
{"LF", 1461520},
{"LG", 1463232},
{"LH", 1464944},
{"LI", 1466656},
{"LJ", 1468368},
{"LK", 1470080},
{"LL", 1471792},
{"LM", 1473504},
{"LN", 1475216},
{"LO", 1476928},
{"LP", 1478640},
{NULL, 0}
};

/**
  *	\brief setupChannels
  *	sets the entries in the GUI
  */
//
//	Note that the ComboBox is GUI specific, but we assume
//	a comboBox is available to act later on as selector
//	for the channels
//
void	RadioInterface::setupChannels (QComboBox *s, uint8_t band) {
struct dabFrequencies *t;
int16_t	i;
int16_t	c	= s -> count ();
//
//	clear the fields in the conboBox
	for (i = 0; i < c; i ++)
	   s	-> removeItem (c - (i + 1));

	if (band == BAND_III)
	   t = bandIII_frequencies;
	else
	   t = Lband_frequencies;

	for (i = 0; t [i]. key != NULL; i ++)
	   s -> insertItem (i, t [i]. key, QVariant (i));
}

static 
const char *table12 [] = {
"none",
"news",
"current affairs",
"information",
"sport",
"education",
"drama",
"arts",
"science",
"talk",
"pop music",
"rock music",
"easy listening",
"light classical",
"classical music",
"other music",
"wheather",
"finance",
"children\'s",
"factual",
"religion",
"phone in",
"travel",
"leisure",
"jazz and blues",
"country music",
"national music",
"oldies music",
"folk music",
"entry 29 not used",
"entry 30 not used",
"entry 31 not used"
};

const char *RadioInterface::get_programm_type_string (uint8_t type) {
	if (type > 0x40) {
	   fprintf (stderr, "GUI: programmtype wrong (%d)\n", type);
	   return (table12 [0]);
	}

	return table12 [type];
}

static
const char *table9 [] = {
"unknown",
"Albanian",
"Breton",
"Catalan",
"Croatian",
"Welsh",
"Czech",
"Danish",
"German",
"English",
"Spanish",
"Esperanto",
"Estonian",
"Basque",
"Faroese",
"French",
"Frisian",
"Irish",
"Gaelic",
"Galician",
"Icelandic",
"Italian",
"Lappish",
"Latin",
"Latvian",
"Luxembourgian",
"Lithuanian",
"Hungarian",
"Maltese",
"Dutch",
"Norwegian",
"Occitan",
"Polish",
"Postuguese",
"Romanian",
"Romansh",
"Serbian",
"Slovak",
"Slovene",
"Finnish",
"Swedish",
"Tuskish",
"Flemish",
"Walloon"
};

const char *RadioInterface::get_programm_language_string (uint8_t language) {
	if (language > 43) {
	   fprintf (stderr, "GUI: wrong language (%d)\n", language);
	   return table9 [0];
	}
	return table9 [language];
}

//
//
//	Most GUI specific things for the initialization are here
void	RadioInterface::init_your_gui (void) {
#ifdef	GUI_3
	ficBlocks		= 0;
	ficSuccess		= 0;

/**
  *	we now handle the settings as saved by previous incarnations.
  */
	QString h		=
	           dabSettings -> value ("device", "no device"). toString ();
	if (h == "no device")	// no autostart here
	   autoStart = false;
    //setDevice 		("dabstick");
    //setDevice 		("rtl_tcp");
    setDevice 		(h);

	h		= dabSettings -> value ("channel", "12C"). toString ();
	
//	display the version
	QString v = "sdr-j DAB-rpi(+)  " ;
	v. append (CURRENT_VERSION);

    dabSettings->endGroup();
#endif
}
//////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
//	
//	The public slots are called from other places within the dab software
//	so please provide some implementation, perhaps an empty one
//
//	a slot called by the ofdmprocessor
void	RadioInterface::set_fineCorrectorDisplay (int v) {
#ifdef	GUI_3
    //finecorrectorDisplay	-> display (v);
#endif
}

//	a slot called by the ofdmprocessor
void	RadioInterface::set_coarseCorrectorDisplay (int v) {
#ifdef	GUI_3
    //coarsecorrectorDisplay	-> display (v);
#endif
}
/**
  *	clearEnsemble
  *	on changing settings, we clear all things in the gui
  *	related to the ensemble.
  *	The function is called from "deep" within the handling code
  *	Potentially a dangerous approach, since the fic handler
  *	might run in a separate thread and generate data to be displayed
  */
void	RadioInterface::clearEnsemble	(void) {
//
//	it obviously means: stop processing
	my_mscHandler		-> stopProcessing ();
	my_ficHandler		-> clearEnsemble ();
	my_ofdmProcessor	-> coarseCorrectorOn ();
	my_ofdmProcessor	-> reset ();

	clear_showElements	();
}

//
//	a slot, called by the fic/fib handlers
void	RadioInterface::addtoEnsemble (const QString &s) {
#ifdef	GUI_3
    // Add new station into list
    if(!s.contains("data") && !stationList.contains(s))
    {
        stationList.append(s, CurrentChannel);

        fprintf(stderr,"Found station %s\n", s.toStdString().c_str());
        emit foundChannelCount(stationList.count());
    }
#endif
}

//
///	a slot, called by the fib processor
void	RadioInterface::nameofEnsemble (int id, const QString &v) {
QString s;
#ifdef	GUI_3
    /*/(void)v;
	ensembleId		-> display (id);
	ensembleLabel		= v;
    ensembleName		-> setText (v);*/
#endif
	my_ofdmProcessor	-> coarseCorrectorOff ();
}

/**
  *	\brief show_successRate
  *	a slot, called by the MSC handler to show the
  *	percentage of frames that could be handled
  */
void	RadioInterface::show_successRate (int s) {
#ifdef	GUI_3
    //errorDisplay	-> display (s);
#endif
}

///	... and the same for the FIC blocks
void	RadioInterface::show_ficCRC (bool b) {
#ifdef	GUI_3
    emit ficFlag(b);
    isFICCRC = b;
#endif
}

///	called from the ofdmDecoder, which computed this for each frame
void	RadioInterface::show_snr (int s) {
#ifdef	GUI_3
    emit signalPower(s);
#endif
}

///	just switch a color, obviously GUI dependent, but called
//	from the ofdmprocessor
void	RadioInterface::setSynced	(char b) {
#ifdef	GUI_3
	if (isSynced == b)
	   return;

	isSynced = b;
	switch (isSynced) {
       case SYNCED:
          /*syncedLabel ->
                   setStyleSheet ("QLabel {background-color : green}");*/
          emit syncFlag(true);
          break;

       default:
          /*syncedLabel ->
                   setStyleSheet ("QLabel {background-color : red}");*/
          emit syncFlag(false);
          break;
	}
#endif
}

//	showLabel is triggered by the message handler
//	the GUI may decide to ignore this
void	RadioInterface::showLabel	(QString s) {
#ifdef	GUI_3
    /*if (running)
       dynamicLabel	-> setText (s);*/
#endif
}
//
//	showMOT is triggered by the MOT handler,
//	the GUI may decide to ignore the data sent
//	since data is only sent whenever a data channel is selected
void	RadioInterface::showMOT		(QString name, QByteArray data, int subtype) {
#ifdef	GUI_3
    emit motChanged(name);
#endif
}

//
//	sendDatagram is triggered by the ip handler,
void	RadioInterface::sendDatagram	(char *data, int length) {
}

/**
  *	\brief changeinConfiguration
  *	No idea yet what to do, so just give up
  *	with what we were doing. The user will -eventually -
  *	see the new configuration from which he can select
  */
void	RadioInterface::changeinConfiguration	(void) {
	if (running) {
	   soundOut		-> stop ();
	   inputDevice		-> stopReader ();
	   inputDevice		-> resetBuffer ();
	   running		= false;
	}
	clear_showElements	();
}

void	RadioInterface::newAudio	(int rate) {
	if (running)
	   soundOut	-> audioOut (rate);
}

//	if so configured, the function might be triggered
//	from the message decoding software. The GUI
//	might decide to ignore the data sent
void	RadioInterface::show_mscErrors	(int er) {
#ifdef	GUI_3
    /*crcErrors_1	-> display (er);
	if (crcErrors_File != 0) 
	   fprintf (crcErrors_File, "%d %% of MSC packets passed crc test\n",
                                                            er);*/
#endif
}
//
//	a slot, called by the iphandler
void	RadioInterface::show_ipErrors	(int er) {
#ifdef	GUI_3
    /*crcErrors_2	-> display (er);
	if (crcErrors_File != 0) 
	   fprintf (crcErrors_File, "%d %% of ip packets passed crc test\n",
                                                            er);*/
#endif
}

void    RadioInterface::setStereo (bool isStereo) {
#ifdef	GUI_3
    emit audioType(isStereo);
#endif
}

void    RadioInterface::setSignalPresent (bool isSignal) {
#ifdef	GUI_3
    isSignalPresent = isSignal;

    emit signalFlag(isSignal);
#endif
}

void    RadioInterface::displayDateTime (int32_t* DateTime) {
#ifdef	GUI_3
    int Year = DateTime[0];
    int Month = DateTime[1];
    int Day = DateTime[2];
    int Hour = DateTime[3];
    int Minute = DateTime[4];

    //fprintf(stderr, "%i:%i:%i  %i:%i\n",Year, Month, Day, Hour, Minute);

    emit newDateTime(Year, Month, Day, Hour, Minute);
#endif
}

//
//	This function is only used in the Gui to clear
//	the details of a selection
void	RadioInterface::clear_showElements (void) {
#ifdef	GUI_3
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////
//	
//	Private slots relate to the modeling of the GUI
//
/**
  *	\brief setStart is a function that is called after pushing
  *	the start button.
  *	if "autoStart" == true, then the initializer will start
  *
  */
void	RadioInterface::setStart	(void) {
bool	r = 0;
	if (running)		// only listen when not running yet
	   return;
//
	r = inputDevice		-> restartReader ();
	qDebug ("Starting %d\n", r);
    if (!r) {
       qDebug ("Opening  input stream failed\n");
	   return;
    }
//
//	Of course, starting the machine will generate a new instance
//	of the ensemble, so the listing - if any - should be cleared
	clearEnsemble ();		// the display
//
///	this does not hurt
	soundOut	-> restart ();
	running = true;
}

/**
  *	\brief TerminateProcess
  *	Pretty critical, since there are many threads involved
  *	A clean termination is what is needed, regardless of the GUI
  */
void	RadioInterface::TerminateProcess (void) {
	running		= false;
#ifdef	GUI_3
#endif
	inputDevice		-> stopReader ();	// might be concurrent
	my_mscHandler		-> stopHandler ();	// might be concurrent
	my_ofdmProcessor	-> stop ();	// definitely concurrent
	soundOut		-> stop ();
//
//	everything should be halted by now
	dumpControlState (dabSettings);
	delete		my_ofdmProcessor;
	delete		my_ficHandler;
	delete		my_mscHandler;
	delete		soundOut;
	soundOut	= NULL;		// signals may be pending, so careful
#ifdef	GUI_3

#endif
	fprintf (stderr, "Termination started\n");
	delete		inputDevice;
    QApplication::quit();
}

//
/**
  *	\brief set_channelSelect
  *	Depending on the GUI the user might select a channel
  *	or some magic will cause a channel to be selected
  */
void	RadioInterface::set_channelSelect (QString s) {
int16_t	i;
struct dabFrequencies *finger;
bool	localRunning	= running;
int32_t	tunedFrequency;

	if (localRunning) {
	   soundOut	-> stop ();
	   inputDevice		-> stopReader ();
	   inputDevice		-> resetBuffer ();
	}

	clear_showElements ();

	tunedFrequency		= 0;
	if (dabBand == BAND_III)
	   finger = bandIII_frequencies;
	else
	   finger = Lband_frequencies;

	for (i = 0; finger [i]. key != NULL; i ++) {
	   if (finger [i]. key == s) {
	      tunedFrequency	= KHz (finger [i]. fKHz);
	      break;
	   }
	}

	if (tunedFrequency == 0)
	   return;

	inputDevice		-> setVFOFrequency (tunedFrequency);

	if (localRunning) {
	   soundOut -> restart ();
	   inputDevice	 -> restartReader ();
	   my_ofdmProcessor	-> reset ();
	   running	 = true;
	}
}

#ifdef	GUI_3
void	RadioInterface::updateTimeDisplay (void) {
}
#endif

#ifdef	GUI_3
void	RadioInterface::autoCorrector_on (void) {
//	first the real stuff
	clear_showElements	();
	my_ficHandler		-> clearEnsemble ();
	my_ofdmProcessor	-> coarseCorrectorOn ();
	my_ofdmProcessor	-> reset ();
}
#endif

#ifdef	GUI_3
//
//	One can imagine that the mode of operation is just selected
//	by the "ini" file, it is pretty unlikely that one changes
//	the mode during operation
void	RadioInterface::set_modeSelect (const QString &s) {
uint8_t	Mode	= s. toInt ();

	running	= false;
	soundOut		-> stop ();
	inputDevice		-> stopReader ();
	my_ofdmProcessor	-> stop ();
//
//	we have to create a new ofdmprocessor with the correct
//	settings of the parameters.
	delete 	my_ofdmProcessor;
	delete	my_mscHandler;
	setModeParameters (Mode);
	my_ficHandler		-> setBitsperBlock	(2 * dabModeParameters. K);
	my_mscHandler		= new mscHandler	(this,
	                                                 &dabModeParameters,
	                                                 audioBuffer,
	                                                 show_crcErrors);
	delete my_ofdmProcessor;
	my_ofdmProcessor	= new ofdmProcessor   (inputDevice,
	                                               &dabModeParameters,
	                                               this,
	                                               my_mscHandler,
	                                               my_ficHandler,
	                                               threshold);
//	and wait for someone push the setStart
}
#endif
//
#ifdef	GUI_3
//	One can imagine that the band of operation is just selected
//	by the "ini" file, it is pretty unlikely that one changes
//	the band during operation
void	RadioInterface::set_bandSelect (QString s) {

	if (running) {
	   running	= false;
	   inputDevice	-> stopReader ();
	   inputDevice	-> resetBuffer ();
	   soundOut	-> stop ();
	   usleep (100);
	   clearEnsemble ();
	}

	if (s == "BAND III")
	   dabBand	= BAND_III;
	else
	   dabBand	= L_BAND;
    //setupChannels (channelSelector, dabBand);
}
#endif
/**
  *	\brief setDevice
  *	setDevice is called trough a signal from the gui
  *	Operation is in three steps: 
  *	   first dumping of any kind is stopped
  *	   second the previously loaded device is stopped
  *	   third, the new device is initiated, but not started
  */
//
//	setDevice is called from the GUI. Other GUI's might have a preselected
//	single device to go with, then if suffices to extract some
//	code specific to that device
#ifdef GUI_3
void	RadioInterface::setDevice (QString s) {
bool	success;

    CurrentDevice = s;
///	indicate that we are not running anymore
	running	= false;
	soundOut	-> stop ();
//
//
///	select. For all it holds that:
	inputDevice	-> stopReader ();
	delete	my_ofdmProcessor;
	delete	inputDevice;
//	dynamicLabel	-> setText ("");
///	OK, everything quiet, now looking what to do
#ifdef	HAVE_AIRSPY
	if (s == "airspy") {
	   inputDevice	= new airspyHandler (dabSettings, &success);
	   if (!success) {
	      delete inputDevice;
	      QMessageBox::warning (this, tr ("sdr"),
	                               tr ("airspy: no luck\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	   else 
	      set_channelSelect	(channelSelector -> currentText ());
	}
	else
#endif
#ifdef HAVE_UHD
//	UHD is - at least in its current setting - for Linux
//	and not tested by me
	if (s == "UHD") {
	   inputDevice = new uhdInput (dabSettings, &success );
	   if (!success) {
	      delete inputDevice;
	      QMessageBox::warning( this, tr ("sdr"), tr ("UHD: no luck\n") );
	      inputDevice = new virtualInput();
	      resetSelector ();
	   }
	   else 
	      set_channelSelect (channelSelector->currentText() );
	}
	else
#endif
#ifdef HAVE_EXTIO
//	extio is - in its current settings - for Windows, it is a
//	wrap around the dll
	if (s == "extio") {
	   inputDevice = new extioHandler (dabSettings, &success);
	   if (!success) {
	      delete inputDevice;
	      QMessageBox::warning( this, tr ("sdr"), tr ("extio: no luck\n") );
	      inputDevice = new virtualInput();
	      resetSelector ();
	   }
	   else 
	      set_channelSelect (channelSelector -> currentText() );
	}
	else
#endif
#ifdef HAVE_RTL_TCP
//	RTL_TCP might be working. 
	if (s == "rtl_tcp") {
	   inputDevice = new rtl_tcp_client (dabSettings, &success);
	   if (!success) {
	      delete inputDevice;
          //QMessageBox::warning( this, tr ("sdr"), tr ("UHD: no luck\n") );
	      inputDevice = new virtualInput();
          //resetSelector ();
	   }
      /* else
          set_channelSelect (channelSelector->currentText() );*/
	}
	else
#endif
#ifdef	HAVE_SDRPLAY
	if (s == "sdrplay") {
	   inputDevice	= new sdrplay (dabSettings, &success);
	   if (!success) {
	      delete inputDevice;
	      QMessageBox::warning (this, tr ("sdr"),
	                               tr ("SDRplay: no library\n"));
	      inputDevice = new virtualInput ();
	      resetSelector ();
	   }
	   else 
	      set_channelSelect	(channelSelector -> currentText ());
	}
	else
#endif
#ifdef	HAVE_DABSTICK
	if (s == "dabstick") {
	   inputDevice	= new dabStick (dabSettings, &success);
	   if (!success) {
	      delete inputDevice;
         /* QMessageBox::warning (this, tr ("sdr"),
                                   tr ("Dabstick: no luck\n"));*/
	      inputDevice = new virtualInput ();
          //resetSelector ();
	   }
      /* else
          set_channelSelect	(channelSelector -> currentText ());*/
	}
	else
#endif
    {	// s == "no device"
//	and as default option, we have a "no device"
	   inputDevice	= new virtualInput ();
	}
///	we have a new device, so we can re-create the ofdmProcessor
///	Note: the fichandler and mscHandler remain unchanged
	my_ofdmProcessor	= new ofdmProcessor   (inputDevice,
	                                               &dabModeParameters,
	                                               this,
	                                               my_mscHandler,
	                                               my_ficHandler,
	                                               threshold);
}
#endif

//	Selecting a service. The interface is GUI dependent,
//	most of the actions are not
//
//	Note that the audiodata or the packetdata contains quite some
//	info on the service (i.e. rate, address, etc)
#ifdef	GUI_3
void	RadioInterface::selectService (QModelIndex s) {
QString a;
//QString a = ensemble. data (s, Qt::DisplayRole). toString ();

	switch (my_ficHandler -> kindofService (a)) {
	   case AUDIO_SERVICE:
	      { audiodata d;
	        my_ficHandler	-> dataforAudioService (a, &d);
	        my_mscHandler	-> set_audioChannel (&d);
	        showLabel (QString (" "));
	        break;
	      }
	   case PACKET_SERVICE:
	      {  packetdata d;
	          my_ficHandler	-> dataforDataService (a, &d);
	         if ((d.  DSCTy == 0) || (d. bitRate == 0))
	            return;
	         my_mscHandler	-> set_dataChannel (&d);
	         switch (d. DSCTy) {
	            default:
	               showLabel (QString ("unimplemented Data"));
	               break;
	            case 5:
	               showLabel (QString ("Transp. Channel not implemented"));
	               break;
	            case 60:
	               showLabel (QString ("MOT partially implemented"));
	               break;
	            case 59: {
	                  QString text = QString ("Embedded IP: UDP data to ");
	                  text. append (ipAddress);
	                  text. append (" ");
	                  QString n = QString::number (port);
	                  text. append (n);
	                  showLabel (text);
	               }
	               break;
	            case 44:
	               showLabel (QString ("Journaline"));
	               break;
	         }
	        break;
	      }
	   default:
	      return;
	}
}
//
#endif

//	Dumping is GUI dependent and may be ignored
#ifdef	GUI_3
///	switch for dumping on/off
void	RadioInterface::set_dumping (void) {
}
#endif
#ifdef	GUI_3
///	audiodumping is similar
void	RadioInterface::set_audioDump (void) {
}

void    RadioInterface::CheckFICTimerTimeout (void)
{
    if(isFICCRC)
    {
        // Tune to station
        switch (my_ficHandler -> kindofService (CurrentStation))
        {
               case AUDIO_SERVICE:
                  {
                    // Stop timer
                    CheckFICTimer.stop();
                    emit currentStation(CurrentStation.simplified());

                    audiodata d;
                    my_ficHandler	-> dataforAudioService (CurrentStation, &d);
                    my_mscHandler	-> set_audioChannel (&d);
                    showLabel (QString (" "));
                    emit stationType(get_programm_type_string (d.programType));
                    emit languageType(get_programm_language_string (d.language));
                    emit bitrate(d.bitRate);
                    //LabelServiceLabel -> setText (StationName. simplified ());
                    if(d.ASCTy == 077)
                        emit dabType("DAB+");
                    else
                        emit dabType("DAB");
                    break;
                  }
            }
    }
}

void	RadioInterface::channelClick(QString StationName, QString ChannelName)
{
    setStart ();
    if (ChannelName != CurrentChannel)
    {
       set_channelSelect (ChannelName);
       CurrentChannel = ChannelName;
    }

    CurrentStation = StationName;

    // Start the checking of the FIC CRC. If the FIC CRC is ok we can tune to the channel
    CheckFICTimer.start(1000);

    emit currentStation("Tuning ...");
}

void	RadioInterface::startChannelScanClick(void)
{
    BandIIIChannelIt = 0;
    BandLChannelIt = 0;

    // Clear old channels
    stationList.reset();
    emit foundChannelCount(0);

    // Set first state
    ScanChannelState = ScanStart;

    // Start channel scan
    ScanChannelTimer.start(1000);
}

void	RadioInterface::stopChannelScanClick(void)
{
    // Stop channel scan
    //ScanChannelTimer.stop();
    ScanChannelState = ScanDone;
}



void	RadioInterface::scanChannelTimerTimeout(void)
{
    static int Timeout = 0;

    // **** The channel scan is done by a simple state machine ***

    // State ScanStart
    if(ScanChannelState == ScanStart)
    {
        //fprintf(stderr,"State: ScanStart\n");

        // Open and start the radio
        setStart ();

        // Reset the station list
        //StationList.clear();
        stationList.reset();

        ScanChannelState = ScanTunetoChannel;
    }

    // State ScanTunetoChannel
    if(ScanChannelState == ScanTunetoChannel)
    {
        //fprintf(stderr,"State: ScanTunetoChannel\n");

        // Select channel
        if(BandIIIChannelIt < 38) // 38 band III frequencies
        {
            CurrentChannel = bandIII_frequencies [BandIIIChannelIt]. key;
            dabBand	= BAND_III;
            fprintf(stderr,"Scan channel: %s, %d kHz\n", bandIII_frequencies [BandIIIChannelIt]. key, bandIII_frequencies [BandIIIChannelIt].fKHz);
            emit channelScanProgress(BandIIIChannelIt);

            // Tune to channel
            set_channelSelect (CurrentChannel);

            /*if(BandIIIChannelIt == 0)
                BandIIIChannelIt = 1;
            if(BandIIIChannelIt == 2)
                BandIIIChannelIt = 26;*/
            BandIIIChannelIt ++;
            Timeout = 0;
            ScanChannelState = ScanCheckSignal;
        }
        /*else if(BandLChannelIt < 16) // 16 L band frequencies
        {
            CurrentChannel = Lband_frequencies [BandLChannelIt]. key;
            dabBand	= L_BAND;
            fprintf(stderr,"Scan channel: %s, %d kHz\n", Lband_frequencies [BandLChannelIt]. key, Lband_frequencies [BandLChannelIt].fKHz);
            BandLChannelIt++;
            emit channelScanProgress(BandIIIChannelIt + BandLChannelIt);

            // Tune to channel
            set_channelSelect (CurrentChannel);

            Timeout = 0;
            ScanChannelState = ScanCheckSignal;
        }*/
        else
        {
            ScanChannelState = ScanDone;
        }

        emit currentStation("Scanning " + CurrentChannel + " ...");
    }

    // State ScanCheckSignal
    if(ScanChannelState == ScanCheckSignal)
    {
        //fprintf(stderr,"State: ScanCheckSignal\n");

        if(isSignalPresent)
        {
            Timeout = 0;
            ScanChannelState = ScanWaitForFIC;
        }
        else
        {
            Timeout++;
        }

        // 2 s timeout
        if(Timeout >= 2)
        {
            //fprintf(stderr,"ScanCheckSignal Timeout\n");
            ScanChannelState = ScanTunetoChannel;
        }
    }

    // State ScanWaitForFIC
    if(ScanChannelState == ScanWaitForFIC)
    {
        //fprintf(stderr,"State: ScanWaitForFIC\n");

        if(isFICCRC)
        {
            fprintf(stderr,"Found channel %s\n", CurrentChannel.toStdString().c_str());

            Timeout = 0;
            ScanChannelState = ScanWaitForChannelNames;
        }
        else
        {
            Timeout++;
        }

        // 30 s timeout
        if(Timeout >= 30)
        {
            //fprintf(stderr,"ScanWaitForFIC Timeout\n");
            ScanChannelState = ScanTunetoChannel;
        }
    }

    // State ScanWaitForChannelNames
    if(ScanChannelState == ScanWaitForChannelNames)
    {
        Timeout++;

        // 30 s timeout
        if(Timeout >= 30)
            ScanChannelState = ScanTunetoChannel;
    }

    // State ScanDone
    if(ScanChannelState == ScanDone)
    {
        //fprintf(stderr,"Stop channel scan\n");
        ScanChannelTimer.stop();
        emit channelScanStopped();
        emit currentStation("No Station");

        /*StationList.sort();
        for(int i=0;i<StationList.count();i++)
        {
            QString Station = StationList.at(i);
            fprintf(stderr,"Station: %s\n",Station.toStdString().c_str());
        }*/

        // Sort stations
        stationList.sort();

        // Load stations into GUI
        QQmlContext *rootContext = engine->rootContext();
        rootContext->setContextProperty("stationModel", QVariant::fromValue(stationList.getList()));
    }
}

void RadioInterface::saveSettings(void)
{
    // Save settings
    dumpControlState(dabSettings);
}

#endif
