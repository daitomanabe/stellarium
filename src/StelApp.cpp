/*
 * Stellarium
 * Copyright (C) 2006 Fabien Chereau
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
 
#include <cstdlib>
#include "StelApp.hpp"
#include "StelMainWindow.hpp"

#include "StelCore.hpp"
#include "StelUtils.hpp"
#include "StelTextureMgr.hpp"
#include "LoadingBar.hpp"
#include "StelObjectMgr.hpp"

#include "TelescopeMgr.hpp"
#include "ConstellationMgr.hpp"
#include "NebulaMgr.hpp"
#include "LandscapeMgr.hpp"
#include "GridLinesMgr.hpp"
#include "MilkyWay.hpp"
#include "MeteorMgr.hpp"
#include "StarMgr.hpp"
#include "SolarSystem.hpp"
#include "StelIniParser.hpp"
#include "Projector.hpp"

#include "StelModuleMgr.hpp"
#include "StelFontMgr.hpp"
#include "StelLocaleMgr.hpp"
#include "StelSkyCultureMgr.hpp"
#include "MovementMgr.hpp"
#include "StelFileMgr.hpp"
#include "QtScriptMgr.hpp"

#include <QStringList>
#include <QString>
#include <QFile>
#include <QRegExp>
#include <QTextStream>
#include <set>
#include <string>

// Initialize static variables
StelApp* StelApp::singleton = NULL;

/*************************************************************************
 Create and initialize the main Stellarium application.
*************************************************************************/
StelApp::StelApp(int argc, char** argv, QObject* parent) : QObject(parent),
	maxfps(10000.f), core(NULL), fps(0), frame(0), timefr(0.), 
	timeBase(0.), flagNightVision(false), configFile("config.ini"), confSettings(NULL), initialized(false)
{
	setObjectName("StelApp");
			

	skyCultureMgr=NULL;
	localeMgr=NULL;
	fontManager=NULL;
	stelObjectMgr=NULL;
	textureMgr=NULL;
	moduleMgr=NULL;
	loadingBar=NULL;
	
	// Can't create 2 StelApp instances
	assert(!singleton);
	singleton = this;
	
	argList = new QStringList;
	for(int i=0; i<argc; i++)
		*argList << argv[i];
	
	stelFileMgr = new StelFileMgr();
	
	qtime = new QTime();
	qtime->start();
	
	// Load language codes
	try
	{
		Translator::init(stelFileMgr->findFile("data/iso639-1.utf8"));
	}
	catch (exception& e)
	{
		cerr << "ERROR while loading translations: " << e.what() << endl;
	}
	
	// Parse for first set of CLI arguments - stuff we want to process before other
	// output, such as --help and --version, and if we want to set the configFile value.
	parseCLIArgsPreConfig();
	
	// OK, print the console splash and get on with loading the program
	cout << " -------------------------------------------------------" << endl;
	cout << "[ This is "<< qPrintable(StelApp::getApplicationName()) << " - http://www.stellarium.org ]" << endl;
	cout << "[ Copyright (C) 2000-2008 Fabien Chereau et al         ]" << endl;
	cout << " -------------------------------------------------------" << endl;
	
	QStringList p=stelFileMgr->getSearchPaths();
	cout << "File search paths:" << endl;
	int n=0;
	foreach (QString i, p)
	{
		cout << " " << n << ". " << qPrintable(i) << endl;
		++n;
	}
	cout << "Config file is: " << qPrintable(configFile) << endl;
	
	if (!stelFileMgr->exists(configFile))
	{
		cerr << "config file \"" << qPrintable(configFile) << "\" does not exist - copying the default file." << endl;
		copyDefaultConfigFile();
	}

	// Load the configuration file
	confSettings = new QSettings(getConfigFilePath(), StelIniFormat);
	
	// Main section
	string version = confSettings->value("main/version").toString().toStdString();
	
	if (version.empty())
	{
		qWarning() << "Found an invalid config file. Overwrite with default.";
		delete confSettings;
		QFile::remove(getConfigFilePath());
		copyDefaultConfigFile();
		confSettings = new QSettings(getConfigFilePath(), StelIniFormat);
		// get the new version value from the updated config file
		version = confSettings->value("main/version").toString().toStdString();
	}
	
	if (version!=string(PACKAGE_VERSION))
	{
		std::istringstream istr(version);
		char tmp;
		int v1 =0;
		int v2 =0;
		istr >> v1 >> tmp >> v2;

		// Config versions less than 0.6.0 are not supported, otherwise we will try to use it
		if( v1 == 0 && v2 < 6 )
		{
			// The config file is too old to try an importation
			cout << "The current config file is from a version too old for parameters to be imported (" 
					<< (version.empty() ? "<0.6.0" : version.c_str()) << ")." << endl 
					<< "It will be replaced by the default config file." << endl;

			delete confSettings;
			QFile::remove(getConfigFilePath());
			copyDefaultConfigFile();
			confSettings = new QSettings(getConfigFilePath(), StelIniFormat);
		}
		else
		{
			cout << "Attempting to use an existing older config file." << endl;
		}
	}
	
	parseCLIArgsPostConfig();
	
	core = new StelCore();
	core->initProj();
	moduleMgr = new StelModuleMgr();
}

/*************************************************************************
 Deinitialize and destroy the main Stellarium application.
*************************************************************************/
StelApp::~StelApp()
{
	delete loadingBar; loadingBar=NULL;
	delete core; core=NULL;
	delete skyCultureMgr; skyCultureMgr=NULL;
	delete localeMgr; localeMgr=NULL;
	delete fontManager; fontManager=NULL;
	delete stelObjectMgr; stelObjectMgr=NULL;
	delete stelFileMgr; stelFileMgr=NULL;
	delete moduleMgr; moduleMgr=NULL;	// Also delete all modules
	delete textureMgr; textureMgr=NULL;
	delete argList; argList=NULL;
	delete qtime; qtime=NULL;
}

/*************************************************************************
 Return the full name of stellarium, i.e. "stellarium 0.9.0"
*************************************************************************/
QString StelApp::getApplicationName()
{
	return QString("Stellarium")+" "+PACKAGE_VERSION;
}


void StelApp::init()
{
	textureMgr = new StelTextureMgr();
	localeMgr = new StelLocaleMgr();
	fontManager = new StelFontMgr();
	skyCultureMgr = new StelSkyCultureMgr();
	
	time_multiplier = 1;
	
	// Initialize AFTER creation of openGL context
	textureMgr->init();
	
	maxfps = confSettings->value("video/maximum_fps",10000.).toDouble();
	minfps = confSettings->value("video/minimum_fps",10000.).toDouble();

	loadingBar = new LoadingBar(core->getProjection(), 12., "logo24bits.png",
	              core->getProjection()->getViewportWidth(), core->getProjection()->getViewportHeight(),
	              StelUtils::stringToWstring(PACKAGE_VERSION), 45, 320, 121);
	
	// Stel Object Data Base manager
	stelObjectMgr = new StelObjectMgr();
	stelObjectMgr->init();
	getModuleMgr().registerModule(stelObjectMgr);
	
	localeMgr->init();
	skyCultureMgr->init();
	
	// Init the solar system first
	SolarSystem* ssystem = new SolarSystem();
	ssystem->init();
	getModuleMgr().registerModule(ssystem);
	
	// Load hipparcos stars & names
	StarMgr* hip_stars = new StarMgr();
	hip_stars->init();
	getModuleMgr().registerModule(hip_stars);	
	
	core->init();

	// Init nebulas
	NebulaMgr* nebulas = new NebulaMgr();
	nebulas->init();
	getModuleMgr().registerModule(nebulas);
	
	// Init milky way
	MilkyWay* milky_way = new MilkyWay();
	milky_way->init();
	getModuleMgr().registerModule(milky_way);
	
	// Telescope manager
	TelescopeMgr* telescope_mgr = new TelescopeMgr();
	telescope_mgr->init();
	getModuleMgr().registerModule(telescope_mgr);
	
	// Constellations
	ConstellationMgr* asterisms = new ConstellationMgr(hip_stars);
	asterisms->init();
	getModuleMgr().registerModule(asterisms);
	
	// Landscape, atmosphere & cardinal points section
	LandscapeMgr* landscape = new LandscapeMgr();
	landscape->init();
	getModuleMgr().registerModule(landscape);

	GridLinesMgr* gridLines = new GridLinesMgr();
	gridLines->init();
	getModuleMgr().registerModule(gridLines);
	
	// Meteors
	MeteorMgr* meteors = new MeteorMgr(10, 60);
	meteors->init();
	getModuleMgr().registerModule(meteors);

// ugly fix by Johannes: call skyCultureMgr->init twice so that
// star names are loaded again
	skyCultureMgr->init();
	
	// Initialisation of the color scheme
	flagNightVision=true;  // fool caching
	setVisionModeNight(false);
	setVisionModeNight(confSettings->value("viewing/flag_night").toBool());
	
	// Load dynamically all the modules found in the modules/ directories
	// which are configured to be loaded at startup
	foreach (StelModuleMgr::ExternalStelModuleDescriptor i, moduleMgr->getExternalModuleList())
	{
		if (i.loadAtStartup==false)
			continue;
		StelModule* m = moduleMgr->loadExternalPlugin(i.key);
		if (m!=NULL)
		{
			m->init();
			moduleMgr->registerModule(m);
		}
	}
	
	// Generate dependency Lists for all modules
	moduleMgr->generateCallingLists();
	
	updateI18n();
	
	//QtScriptMgr scriptMgr;
	//scriptMgr.test();
	initialized = true;
}

void StelApp::parseCLIArgsPreConfig(void)
{	
	if (argsGetOption(argList, "-v", "--version"))
	{
		cout << qPrintable(getApplicationName()) << endl;
		exit(0);
	}

	if (argsGetOption(argList, "-h", "--help"))
	{
		// Get the basename of binary
		QString binName = argList->at(0);
		binName.remove(QRegExp("^.*[/\\\\]"));
		
		cout << "Usage:" << endl
				<< "  " 
				<< qPrintable(binName) << " [options]" << endl << endl
				<< "Options:" << endl
				<< "--version (or -v)       : Print program name and version and exit." << endl
				<< "--help (or -h)          : This cruft." << endl
				<< "--config-file (or -c)   : Use an alternative name for the config file" << endl
				<< "--full-screen (or -f)   : With argument \"yes\" or \"no\" over-rides" << endl
				<< "                          the full screen setting in the config file" << endl
				<< "--home-planet           : Specify observer planet (English name)" << endl
				<< "--altitude              : Specify observer altitude in meters" << endl
				<< "--longitude             : Specify longitude, e.g. +53d58\\'16.65\\\"" << endl
				<< "--latitude              : Specify latitude, e.g. -1d4\\'27.48\\\"" << endl 
				<< "--list-landscapes       : Print a list of value landscape IDs" << endl 
				<< "--landscape             : Start using landscape whose ID (dir name)" << endl
				<< "                          is passed as parameter to option" << endl
				<< "--sky-date              : Specify sky date in format yyyymmdd" << endl
				<< "--sky-time              : Specify sky time in format hh:mm:ss" << endl
				<< "--fov                   : Specify the field of view (degrees)" << endl
				<< "--projection-type       : Specify projection type, e.g. stereographic" << endl;
		exit(0);
	}
	
	if (argsGetOption(argList, "", "--list-landscapes"))
	{
		QSet<QString> landscapeIds = stelFileMgr->listContents("landscapes", StelFileMgr::DIRECTORY);
		for(QSet<QString>::iterator i=landscapeIds.begin(); i!=landscapeIds.end(); ++i)
		{
			try 
			{
				// finding the file will throw an exception if it is not found
				// in that case we won't output the landscape ID as it canont work
				stelFileMgr->findFile("landscapes/" + *i + "/landscape.ini");
				cout << (*i).toUtf8().data() << endl;
			}
			catch(exception& e){}
		}
		exit(0);
	}
	
	try
	{
		setConfigFile(qPrintable(argsGetOptionWithArg<QString>(argList, "-c", "--config-file", "config.ini")));
	}
	catch(exception& e)
	{
		cerr << "ERROR: while looking for --config-file option: " << e.what() << ". Using \"config.ini\"" << endl;
		setConfigFile("config.ini");		
	}
}

void StelApp::parseCLIArgsPostConfig()
{
	// Over-ride config file options with command line options
	// We should catch exceptions from argsGetOptionWithArg...
	int fullScreen, altitude;
	float fov;
	QString landscapeId, homePlanet, longitude, latitude, skyDate, skyTime, projectionType;
	
	try
	{
		fullScreen = argsGetYesNoOption(argList, "-f", "--full-screen", -1);
		landscapeId = argsGetOptionWithArg<QString>(argList, "", "--landscape", "");
		homePlanet = argsGetOptionWithArg<QString>(argList, "", "--home-planet", "");
		altitude = argsGetOptionWithArg<int>(argList, "", "--altitude", -1);
		longitude = argsGetOptionWithArg<QString>(argList, "", "--longitude", "");
		latitude = argsGetOptionWithArg<QString>(argList, "", "--latitude", "");
		skyDate = argsGetOptionWithArg<QString>(argList, "", "--sky-date", "");
		skyTime = argsGetOptionWithArg<QString>(argList, "", "--sky-time", "");
		fov = argsGetOptionWithArg<float>(argList, "", "--fov", -1.0);
		projectionType = argsGetOptionWithArg<QString>(argList, "", "--projection-type", "");

	}
	catch (exception& e)
	{
		cerr << "ERROR while checking command line options: " << e.what() << endl;
		exit(0);
	}

	// Will be -1 if option is not found, in which case we don't change anything.
	if (fullScreen == 1) confSettings->setValue("video/fullscreen", true);
	else if (fullScreen == 0) confSettings->setValue("video/fullscreen", false);
	
	if (landscapeId != "") confSettings->setValue("init_location/landscape_name", landscapeId);
	
	if (homePlanet != "") confSettings->setValue("init_location/home_planet", homePlanet);
	
	if (altitude != -1) confSettings->setValue("init_location/altitude", altitude);
	
	QRegExp longLatRx("[\\-+]?\\d+d\\d+\\'\\d+(\\.\\d+)?\"");
	if (longitude != "")
	{
		if (longLatRx.exactMatch(longitude))
			confSettings->setValue("init_location/longitude", longitude);
		else
			cerr << "WARNING: --longitude argument has unrecognised format" << endl;
	}
	
	if (latitude != "")
	{
		if (longLatRx.exactMatch(latitude))
			confSettings->setValue("init_location/latitude", latitude);
		else
			cerr << "WARNING: --latitude argument has unrecognised format" << endl;
	}
	
	if (skyDate != "" || skyTime != "")
	{
		// Get the Julian date for the start of the current day
		// and the extra necessary for the time of day as separate
		// components.  Then if the --sky-date and/or --sky-time flags
		// are set we over-ride the component, and finally add them to 
		// get the full julian date and set that.
		
		// First, lets determine the Julian day number and the part for the time of day
		QDateTime now = QDateTime::currentDateTime();
		double skyDatePart = now.date().toJulianDay();
		double skyTimePart = StelUtils::qTimeToJDFraction(now.time());
		
		// Over-ride the sktDatePart if the user specified the date using --sky-date
		if (skyDate != "")
		{
			// validate the argument format, we will tolerate yyyy-mm-dd by removing all -'s
			QRegExp dateRx("\\d{8}");
			if (dateRx.exactMatch(skyDate.remove("-")))
				skyDatePart = QDate::fromString(skyDate, "yyyyMMdd").toJulianDay();
			else
				cerr << "WARNING: --sky-date argument has unrecognised format  (I want yyyymmdd)" << endl;
		}
		
		if (skyTime != "")
		{
			QRegExp timeRx("\\d{1,2}:\\d{2}:\\d{2}");
			if (timeRx.exactMatch(skyTime))
				skyTimePart = StelUtils::qTimeToJDFraction(QTime::fromString(skyTime, "hh:mm:ss"));
			else
				cerr << "WARNING: --sky-time argument has unrecognised format (I want hh:mm:ss)" << endl;
		}

		confSettings->setValue("navigation/startup_time_mode", "preset");
		confSettings->setValue("navigation/preset_sky_time", skyDatePart + skyTimePart);
	}

	if (fov > 0.0) confSettings->setValue("navigation/init_fov", fov);
	
	if (projectionType != "") confSettings->setValue("projection/type", projectionType);
}

void StelApp::update(double deltaTime)
{
     if (!initialized)
        return;
	
	++frame;
	timefr+=deltaTime;
	if (timefr-timeBase > 1.)
	{
		fps=(double)frame/(timefr-timeBase);				// Calc the FPS rate
		frame = 0;
		timeBase+=1.;
	}

	// change time rate if needed to fast forward scripts
	deltaTime *= time_multiplier;

	core->update(deltaTime);
	
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_UPDATE))
	{
		i->update(deltaTime);
	}
	
	stelObjectMgr->update(deltaTime);
}

//! Main drawing function called at each frame
double StelApp::draw()
{
     if (!initialized)
        return 0.;
        
    // Clear areas not redrawn by main viewport (i.e. fisheye square viewport)
	glClear(GL_COLOR_BUFFER_BIT);

	core->preDraw();

	// Render all the main objects of stellarium
	double squaredDistance = 0.;
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_DRAW))
	{
		double d = i->draw(core);
		if (d>squaredDistance)
			squaredDistance = d;
	}

	core->postDraw();
	
	return squaredDistance;
}

/*************************************************************************
 Call this when the size of the GL window has changed
*************************************************************************/
void StelApp::glWindowHasBeenResized(int w, int h)
{
	core->getProjection()->windowHasBeenResized(w, h);
	// Send the event to every StelModule
	foreach (StelModule* iter, moduleMgr->getAllModules())
	{
		iter->glWindowHasBeenResized(w, h);
	}
}

// Handle mouse clics
int StelApp::handleClick(QMouseEvent* event)
{
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_HANDLEMOUSECLICKS))
	{
		if (i->handleMouseClicks(event)==true)
			return 1;
	}
	return 0;
}

// Handle mouse wheel.
int StelApp::handleWheel(QWheelEvent* event)
{
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_HANDLEMOUSECLICKS))
	{
		if (i->handleMouseWheel(event)==true)
			return 1;
	}
	return 0;
}
	
// Handle mouse move
int StelApp::handleMove(QMouseEvent* event)
{
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_HANDLEMOUSEMOVES))
	{
		if (i->handleMouseMoves(event)==true)
			return 1;
	}
	return 0;
}

// Handle key press and release
int StelApp::handleKeys(QKeyEvent* event)
{
	// Send the event to every StelModule
	foreach (StelModule* i, moduleMgr->getCallOrders(StelModule::ACTION_HANDLEKEYS))
	{
		if (i->handleKeys(event)==true)
			return 1;
	}
	return 0;
}


void StelApp::setConfigFile(const QString& configName)
{
	try
	{
		configFile = stelFileMgr->findFile(configName, StelFileMgr::FLAGS(StelFileMgr::WRITABLE|StelFileMgr::FILE));
		return;
	}
	catch(exception& e)
	{
		//cerr << "DEBUG StelApp::setConfigFile could not locate writable config file " << configName << endl;
	}
	
	try
	{
		configFile = stelFileMgr->findFile(configName, StelFileMgr::FILE);	
		return;
	}
	catch(exception& e)
	{
		//cerr << "DEBUG StelApp::setConfigFile could not find read only config file " << configName << endl;
	}		
	
	try
	{
		configFile = stelFileMgr->findFile(configName, StelFileMgr::NEW);
		//cerr << "DEBUG StelApp::setConfigFile found NEW file path: " << configFile << endl;
		return;
	}
	catch(exception& e)
	{
		cerr << "ERROR StelApp::setConfigFile could not find or create configuration file " << configName.toUtf8().data() << endl;
		exit(1);
	}
}

void StelApp::copyDefaultConfigFile()
{
	QString defaultConfigFilePath;
	try
	{
		defaultConfigFilePath = stelFileMgr->findFile("data/default_config.ini");
	}
	catch(exception& e)
	{
		cerr << "ERROR (copyDefaultConfigFile): failed to locate data/default_config.ini.  Please check your installation." << endl;
		exit(1);
	}
	
	QFile::copy(defaultConfigFilePath, configFile);
	if (!stelFileMgr->exists(configFile))
	{
		cerr << "ERROR (copyDefaultConfigFile): failed to copy file " << defaultConfigFilePath.toUtf8().data() << " to " << configFile.toUtf8().data() << ". You could try to copy it by hand." << endl;
		exit(1);
	}
}

// Set the colorscheme for all the modules
void StelApp::setColorScheme(const QString& section)
{
	// Send the event to every StelModule
	foreach (StelModule* iter, moduleMgr->getAllModules())
	{
		iter->setColorScheme(confSettings, section);
	}
}

//! Set flag for activating night vision mode
void StelApp::setVisionModeNight(bool b)
{
	if (flagNightVision!=b)
	{
		setColorScheme(b ? "night_color" : "color");
	}
	flagNightVision=b;
}

// Update translations and font for sky everywhere in the program
void StelApp::updateI18n()
{
	// Send the event to every StelModule
	foreach (StelModule* iter, moduleMgr->getAllModules())
	{
		iter->updateI18n();
	}
}

// Update and reload sky culture informations everywhere in the program
void StelApp::updateSkyCulture()
{
	// Send the event to every StelModule
	foreach (StelModule* iter, moduleMgr->getAllModules())
	{
		iter->updateSkyCulture();
	}
}

bool StelApp::argsGetOption(QStringList* args, QString shortOpt, QString longOpt)
{
	bool result=false;

        // Don't see anything after a -- as an option
	int lastOptIdx = args->indexOf("--");
	if (lastOptIdx == -1)
		lastOptIdx = args->size();

	for(int i=0; i<lastOptIdx; i++)
	{
		if ((shortOpt!="" && args->at(i) == shortOpt) || args->at(i) == longOpt)
		{
			result = true;
			i=args->size();
		}
	}

	return result;
}

template<class T>
T StelApp::argsGetOptionWithArg(QStringList* args, QString shortOpt, QString longOpt, T defaultValue)
{
	// Don't see anything after a -- as an option
	int lastOptIdx = args->indexOf("--");
	if (lastOptIdx == -1)
		lastOptIdx = args->size();

	for(int i=0; i<lastOptIdx; i++)
	{
		bool match(false);
		QString argStr("");

		// form -n=arg
		if ((shortOpt!="" && args->at(i).left(shortOpt.length()+1)==shortOpt+"="))
		{
			match=true;
			argStr=args->at(i).right(args->at(i).length() - shortOpt.length() - 1);
		}
		// form --number=arg
		else if (args->at(i).left(longOpt.length()+1)==longOpt+"=")
		{
			match=true;
			argStr=args->at(i).right(args->at(i).length() - longOpt.length() - 1);
		}
		// forms -n arg and --number arg
		else if ((shortOpt!="" && args->at(i)==shortOpt) || args->at(i)==longOpt)
		{
			if (i+1>=lastOptIdx)
			{
				throw(runtime_error(qPrintable("optarg_missing ("+longOpt+")")));
			}
			else
			{
				match=true;
				argStr=args->at(i+1);
				i++;  // skip option argument in next iteration 
			}
		}

		if (match)
		{
			T retVal;
			QTextStream converter(qPrintable(argStr));
			converter >> retVal;
			if (converter.status() != QTextStream::Ok)
				throw(runtime_error(qPrintable("optarg_type ("+longOpt+")")));
			else
				return retVal;
		}
	}

	return defaultValue;
}

int StelApp::argsGetYesNoOption(QStringList* args, QString shortOpt, QString longOpt, int defaultValue)
{
	QString strArg = argsGetOptionWithArg<QString>(args, shortOpt, longOpt, "");
	if (strArg == "")
	{
		return defaultValue;
	}
	if (strArg.compare("yes", Qt::CaseInsensitive)==0
		   || strArg.compare("y", Qt::CaseInsensitive)==0
		   || strArg.compare("true", Qt::CaseInsensitive)==0
		   || strArg.compare("t", Qt::CaseInsensitive)==0
		   || strArg.compare("on", Qt::CaseInsensitive)==0
		   || strArg=="1")
	{
		return 1;
	}
	else if (strArg.compare("no", Qt::CaseInsensitive)==0
			|| strArg.compare("n", Qt::CaseInsensitive)==0
			|| strArg.compare("false", Qt::CaseInsensitive)==0
			|| strArg.compare("f", Qt::CaseInsensitive)==0
			|| strArg.compare("off", Qt::CaseInsensitive)==0
			|| strArg=="0")
	{
		return 0;
	}
	else
	{
		throw(runtime_error("optarg_type"));
	}
}

//! Return the time since when stellarium is running in second.
double StelApp::getTotalRunTime() const
{
	return (double)qtime->elapsed()/1000;
}

