/*
 * Stellarium
 * Copyright (C) 2002 Fabien Chereau
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

// class used to manage groups of Nebulas

#include <fstream>
#include "nebula_mgr.h"
#include "stellarium.h"
#include "s_texture.h"
#include "s_font.h"
#include "navigator.h"
#include "translator.h"

#define RADIUS_NEB 1.

NebulaMgr::NebulaMgr()
{
	nebZones = new vector<Nebula*>[nebGrid.getNbPoints()];
}

NebulaMgr::~NebulaMgr()
{
	vector<Nebula *>::iterator iter;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		delete (*iter);
	}

	if (Nebula::tex_circle) delete Nebula::tex_circle;
	Nebula::tex_circle = NULL;

	if (Nebula::nebula_font) delete Nebula::nebula_font;
	Nebula::nebula_font = NULL;
}

// read from stream
bool NebulaMgr::read(float font_size, const string& font_name, const string& catNGC, const string& catNGCNames, const string& catTextures, LoadingBar& lb)
{
	loadNGC(catNGC, lb);
	loadNGCNames(catNGCNames);
	loadTextures(catTextures, lb);

	if (!Nebula::nebula_font) Nebula::nebula_font = new s_font(font_size, font_name); // load Font
	if (!Nebula::nebula_font)
	{
		printf("Can't create nebulaFont\n");
		return(1);
	}

	if (!Nebula::tex_circle)
		Nebula::tex_circle = new s_texture("neb.png");   // Load circle texture

	return true;
}

// Draw all the Nebulaes
void NebulaMgr::draw(Projector* prj, const Navigator * nav, ToneReproductor* eye)
{
	Nebula::hints_brightness = hintsFader.getInterstate()*flagShow.getInterstate();

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	
	glBlendFunc(GL_ONE, GL_ONE);
	// if (draw_mode == DM_NORMAL) glBlendFunc(GL_ONE, GL_ONE);
	// else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // charting

	Vec3f pXYZ;


// 	vector<Nebula *>::iterator iter;
// 	for(iter=neb_array.begin();iter!=neb_array.end();iter++)

	// Find the star zones which are in the screen
	int nbZones=0;
	// FOV is currently measured vertically, so need to adjust for wide screens
	// TODO: projector should probably use largest measurement itself
	float max_fov = MY_MAX( prj->get_fov(), prj->get_fov()*prj->getViewportWidth()/prj->getViewportHeight());
	nbZones = nebGrid.Intersect(nav->get_equ_vision(), max_fov*M_PI/180.f*1.2f);
	static int * zoneList = nebGrid.getResult();
	
    prj->set_orthographic_projection();	// set 2D coordinate

	// Print all the stars of all the selected zones
	static vector<Nebula *>::iterator end;
	static vector<Nebula *>::iterator iter;
	Nebula* n;

	for(int i=0;i<nbZones;++i)
	{
		end = nebZones[zoneList[i]].end();
	    for(iter = nebZones[zoneList[i]].begin(); iter!=end; ++iter)
		{
			n = *iter;
			// improve performance by skipping if too small to see
			if (n->get_on_screen_size(prj, nav)>5 || (hintsFader.getInterstate()>0.0001  && n->mag <= getMaxMagHints()))
			{
				// correct for precession
				pXYZ = nav->j2000_to_earth_equ(n->XYZ);
	
				// project in 2D to check if the nebula is in screen
				if ( !prj->project_earth_equ_check(pXYZ,n->XY) ) continue;
	
				if (n->hasTex()) n->draw_tex(prj, nav, eye);
				else n->draw_no_tex(prj, nav, eye);
	
				if (hintsFader.getInterstate()>0.00001 && n->mag <= getMaxMagHints())
				{
					n->draw_name(prj);
					n->draw_circle(prj, nav);
				}
			}
		}
	}
	prj->reset_perspective_projection();
}

// search by name
StelObject * NebulaMgr::search(const string& name)
{
	/*const string catalogs("NGC IC CW SH");*/
	const string catalogs("M NGC IC UGC");
	string cat;
	unsigned int num;

	string n = name;
	for (string::size_type i=0;i<n.length();++i)
	{
		if (n[i]=='_') n[i]=' ';
	}

	istringstream ss(n);
	ss >> cat;
	// check if a valid catalog reference
	if (catalogs.find(cat,0) == string::npos)
		return NULL;
	ss >> num;
	if (ss.fail()) return NULL;
	
	if (cat == "M") return searchM(num);
	if (cat == "NGC") return searchNGC(num);
	if (cat == "IC") return searchIC(num);
	/*if (cat == "UGC") return searchUGC(num);*/
	return NULL;
}


// Look for a nebulae by XYZ coords
StelObject * NebulaMgr::search(Vec3f Pos)
{
	Pos.normalize();
	vector<Nebula *>::iterator iter;
	Nebula * plusProche=NULL;
	float anglePlusProche=0.;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		if ((*iter)->XYZ[0]*Pos[0]+(*iter)->XYZ[1]*Pos[1]+(*iter)->XYZ[2]*Pos[2]>anglePlusProche)
		{
			anglePlusProche=(*iter)->XYZ[0]*Pos[0]+(*iter)->XYZ[1]*Pos[1]+(*iter)->XYZ[2]*Pos[2];
			plusProche=(*iter);
		}
	}
	if (anglePlusProche>RADIUS_NEB*0.999)
	{
		return plusProche;
	}
	else return NULL;
}

// Return a stl vector containing the nebulas located inside the lim_fov circle around position v
vector<StelObject*> NebulaMgr::search_around(Vec3d v, double lim_fov)
{
	vector<StelObject*> result;
	v.normalize();
	double cos_lim_fov = cos(lim_fov * M_PI/180.);
	static Vec3d equPos;

	vector<Nebula*>::iterator iter = neb_array.begin();
	while (iter != neb_array.end())
	{
		equPos = (*iter)->XYZ;
		equPos.normalize();
		if (equPos[0]*v[0] + equPos[1]*v[1] + equPos[2]*v[2]>=cos_lim_fov)
		{
			result.push_back(*iter);
		}
		iter++;
	}
	return result;
}

StelObject * NebulaMgr::searchM(unsigned int M)
{
	vector<Nebula *>::iterator iter;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		if ((*iter)->M_nb == M) return (*iter);
	}

	return NULL;
}

StelObject * NebulaMgr::searchNGC(unsigned int NGC)
{
	vector<Nebula *>::iterator iter;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		if ((*iter)->NGC_nb == NGC) return (*iter);
	}

	return NULL;
}

StelObject * NebulaMgr::searchIC(unsigned int IC)
{
	vector<Nebula *>::iterator iter;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		if ((*iter)->IC_nb == IC) return (*iter);
	}

	return NULL;
}

/*StelObject * NebulaMgr::searchUGC(unsigned int UGC)
{
	vector<Nebula *>::iterator iter;
	for(iter=neb_array.begin();iter!=neb_array.end();iter++)
	{
		if ((*iter)->UGC_nb == UGC) return (*iter);
	}

	return NULL;
}*/

// read from stream
bool NebulaMgr::loadNGC(const string& catNGC, LoadingBar& lb)
{
	char recordstr[512];
	unsigned int i;
	unsigned int data_drop;

	cout << "Loading NGC data... ";
	FILE * ngcFile = fopen(catNGC.c_str(),"rb");
	if (!ngcFile)
	{
		cerr << "NGC data file " << catNGC << " not found" << endl;
		return false;
	}

	// read the number of lines
	unsigned int catalogSize = 0;
	while (fgets(recordstr,512,ngcFile)) catalogSize++;
	rewind(ngcFile);

	// Read the NGC entries
	i = 0;
	data_drop = 0;
	while (fgets(recordstr,512,ngcFile)) // temporary for testing
	{
		if (!(i%200) || (i == catalogSize-1))
		{
			// Draw loading bar
			lb.SetMessage(_("Loading NGC catalog: ") + StelUtility::intToWstring((i == catalogSize-1 ? catalogSize : i)) + 
				L"/" + StelUtility::intToWstring(catalogSize));
			lb.Draw((float)i/catalogSize);
		}
		Nebula *e = new Nebula;
		if (!e->readNGC(recordstr)) // reading error
		{
			delete e;
			e = NULL;
			data_drop++;
		}
		else
		{
			neb_array.push_back(e);
			nebZones[nebGrid.GetNearest(e->XYZ)].push_back(e);
		}
		i++;
	}
	fclose(ngcFile);
	printf("(%d items loaded [%d dropped])\n", i, data_drop);

	return true;
}


bool NebulaMgr::loadNGCNames(const string& catNGCNames)
{
	char recordstr[512];
	cout << "Loading NGC name data...";
	FILE * ngcNameFile = fopen(catNGCNames.c_str(),"rb");
	if (!ngcNameFile)
	{
		cerr << "NGC name data file " << catNGCNames << " not found." << endl;
		return false;
	}

	// Read the names of the NGC objects
	int i = 0;
	char n[40];
	int nb, k;
	Nebula *e;

	while (fgets(recordstr,512,ngcNameFile))
	{

		sscanf(&recordstr[38],"%d",&nb);
		if (recordstr[37] == 'I')
		{
			e = (Nebula*)searchIC(nb);
		}
		else
		{
			e = (Nebula*)searchNGC(nb);
		}

		if (e)
		{
			strncpy(n, recordstr, 36);
			// trim the white spaces at the back
			n[36] = '\0';
			k = 36;
			while (n[--k] == ' ' && k > 0)
			{
				n[k+1] = '\0';
			}
			// If the name is not a messier number perhaps one is already
			// defined for this object
			if (strncmp(n, "M ",2))
			{
				if (e->englishName!="")
				{
					ostringstream oss;
					oss << e->englishName << " - " << n;
					e->englishName = oss.str();
				} else {
					e->englishName = n;
				}
			}

			// If it's a messiernumber, we will call it a messier if there is no better name
			if (!strncmp(n, "M ",2))
			{
				istringstream iss(string(n).substr(1));  // remove the 'M'
				ostringstream oss;

				int num;

				// Let us keep the right number in the Messier catalog
				iss >> num;
				e->M_nb=(unsigned int)(num);
				oss << "M" << num;

				if (e->englishName!="") oss << " - " << e->englishName;
				e->englishName = oss.str();


			}
		}
		else
			cerr << endl << "...no position data for " << n;
		i++;
	}
	fclose(ngcNameFile);
	cout << "( " << i << " names loaded)" << endl;

	return true;
}

bool NebulaMgr::loadTextures(const string& fileName, LoadingBar& lb)
{
	cout << "Loading Nebula Textures...";

	std::ifstream inf(fileName.c_str());
	if (!inf.is_open())
	{
		cerr << "Can't open nebula catalog " << fileName << endl;
		return false;
	}

	// determine total number to be loaded for percent complete display
	string record;
	int total=0;
	while(!getline(inf, record).eof())
	{
		++total;
	}
	inf.clear();
	inf.seekg(0);

	printf("(%d textures loaded)\n", total);

	int current = 0;
	int NGC;
	while(!getline(inf, record).eof())
	{
		// Draw loading bar
		++current;
		lb.SetMessage(_("Loading Nebula Textures:") + StelUtility::intToWstring(current) + L"/" + StelUtility::intToWstring(total));
		lb.Draw((float)current/total);

		istringstream istr(record);
		istr >> NGC;

		Nebula *e = (Nebula*)searchNGC(NGC);
		if (e)
		{
			if (!e->readTexture(record)) // reading error
				cerr << "Error while reading texture for nebula " << e->englishName << endl;
		}
	}
	return true;
}

//! @brief Update i18 names from english names according to passed translator
//! The translation is done using gettext with translated strings defined in translations.h
void NebulaMgr::translateNames(Translator& trans)
{
	vector<Nebula*>::iterator iter;
	for( iter = neb_array.begin(); iter < neb_array.end(); iter++ )
	{
		(*iter)->translateName(trans);
	}
}
