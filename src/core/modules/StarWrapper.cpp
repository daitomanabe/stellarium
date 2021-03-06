/*
 * The big star catalogue extension to Stellarium:
 * Author and Copyright: Johannes Gajdosik, 2006, 2007
 * The implementation of most functions in this file
 * (getInfoString,getShortInfoString,...) is taken from
 * Stellarium, Copyright (C) 2002 Fabien Chereau,
 * and therefore the copyright of these belongs to Fabien Chereau.
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#include "StarWrapper.hpp"
#include "ZoneArray.hpp"

#include "StelUtils.hpp"
#include "StelTranslator.hpp"

#include <QTextStream>
#include <limits>

template<typename T> inline bool isNan(T value)
{
	return value != value;
}

template<typename T> inline bool isInf(T value)
{
	return std::numeric_limits<T>::has_infinity && value == std::numeric_limits<T>::infinity();
}

QString StarWrapperBase::getInfoString(const StelCore *core, const InfoStringGroup& flags) const
{
	QString str;
	QTextStream oss(&str);
	double az_app, alt_app;
	StelUtils::rectToSphe(&az_app,&alt_app,getAltAzPosApparent(core));
	Q_UNUSED(az_app);

	if (flags&ObjectType)
	{
		oss << q_("Type: <b>%1</b>").arg(q_("star")) << "<br />";
	}

	if (flags&Magnitude)
	{
		if (core->getSkyDrawer()->getFlagHasAtmosphere() && (alt_app>-3.0*M_PI/180.0)) // Don't show extincted magnitude much below horizon where model is meaningless.
			oss << q_("Magnitude: <b>%1</b> (after extinction: <b>%2</b>)")
			       .arg(QString::number(getVMagnitude(core), 'f', 2))
			       .arg(QString::number(getVMagnitudeWithExtinction(core), 'f', 2)) << "<br>";
		else
			oss << q_("Magnitude: <b>%1</b>").arg(QString::number(getVMagnitude(core), 'f', 2)) << "<br>";
	}
	
	if (flags&Extra)
	{
		oss << q_("Color Index (B-V): <b>%1</b>").arg(QString::number(getBV(), 'f', 2)) << "<br>";
	}
	
	oss << getPositionInfoString(core, flags);

	StelObject::postProcessInfoString(str, flags);

	return str;
}


QString StarWrapper1::getEnglishName(void) const
{
	if (s->getHip())
		return QString("HIP %1").arg(s->getHip());
	return StarWrapperBase::getEnglishName();
}

QString StarWrapper1::getInfoString(const StelCore *core, const InfoStringGroup& flags) const
{
	QString str;

	QTextStream oss(&str);
	const QString varType = StarMgr::getGcvsVariabilityType(s->getHip());
	const int wdsObs = StarMgr::getWdsLastObservation(s->getHip());
	const float wdsPA = StarMgr::getWdsLastPositionAngle(s->getHip());
	const float wdsSep = StarMgr::getWdsLastSeparation(s->getHip());
	const float maxVMag = StarMgr::getGcvsMaxMagnitude(s->getHip());
	const float magFlag = StarMgr::getGcvsMagnitudeFlag(s->getHip());
	const float minVMag = StarMgr::getGcvsMinMagnitude(s->getHip());
	const float min2VMag = StarMgr::getGcvsMinMagnitude(s->getHip(), false);
	const QString photoVSys = StarMgr::getGcvsPhotometricSystem(s->getHip());
	const double vEpoch = StarMgr::getGcvsEpoch(s->getHip());
	const double vPeriod = StarMgr::getGcvsPeriod(s->getHip());
	const int vMm = StarMgr::getGcvsMM(s->getHip());
	if (s->getHip())
	{
		if ((flags&Name) || (flags&CatalogNumber))
			oss << "<h2>";

		const QString commonNameI18 = StarMgr::getCommonName(s->getHip());
		const QString additionalNameI18 = StarMgr::getAdditionalNames(s->getHip());
		const QString sciName = StarMgr::getSciName(s->getHip());
		const QString addSciName = StarMgr::getSciAdditionalName(s->getHip());
		const QString varSciName = StarMgr::getGcvsName(s->getHip());
		const QString wdsSciName = StarMgr::getWdsName(s->getHip());
		const QString crossIndexData = StarMgr::getCrossIdentificationDesignations(s->getHip());
		QStringList designations;
		if (!sciName.isEmpty())
			designations.append(sciName);
		if (!addSciName.isEmpty())
			designations.append(addSciName);
		if (!varSciName.isEmpty() && varSciName!=addSciName && varSciName!=sciName)
			designations.append(varSciName);

		QString hip;
		if (s->hasComponentID())
			hip = QString("HIP %1 %2").arg(s->getHip()).arg(StarMgr::convertToComponentIds(s->getComponentIds()));
		else
			hip = QString("HIP %1").arg(s->getHip());

		designations.append(hip);

		if (!crossIndexData.isEmpty())
			designations.append(crossIndexData);

		if (!wdsSciName.isEmpty() && wdsSciName!=addSciName && wdsSciName!=sciName)
			designations.append(wdsSciName);

		const QString designationsList = designations.join(" - ");

		if (flags&Name)
		{
			if (!commonNameI18.isEmpty())
				oss << commonNameI18;

			if (!additionalNameI18.isEmpty())
				oss << " (" << additionalNameI18 << ")";

			if (!commonNameI18.isEmpty() && !designationsList.isEmpty() && flags&CatalogNumber)
				oss << "<br>";
		}

		if (flags&CatalogNumber)
			oss << designationsList;

		if ((flags&Name) || (flags&CatalogNumber))
			oss << "</h2>";
	}

	bool ebsFlag = false;
	if (flags&ObjectType)
	{
		QString varstartype = "";
		QString startype = "";
		if(!varType.isEmpty())
		{
			if (QString("FU GCAS I IA IB IN INA INB INT IT IN(YY) IS ISA ISB RCB RS SDOR UV UVN WR").contains(varType))
				varstartype = q_("eruptive variable star");
			else if (QString("ACYG BCEP BCEPS CEP CEP(B) CW CWA CWB DCEP DCEPS DSCT DSCTC GDOR L LB LC M PVTEL RPHS RR RR(B) RRAB RRC RV RVA RVB SR SRA SRB SRC SRD SXPHE ZZ ZZA ZZB").contains(varType))
				varstartype = q_("pulsating variable star");
			else if (QString("ACV, ACVO, BY, ELL, FKCOM, PSR, SXARI").contains(varType))
				varstartype = q_("rotating variable star");
			else if (QString("N NA NB NC NL NR SN SNI SNII UG UGSS UGSU UGZ ZAND").contains(varType))
				varstartype = q_("cataclysmic variable star");
			else if (QString("E EA EB EW GS PN RS WD WR AR D DM DS DW K KE KW SD E: E:/WR E/D E+LPB: EA/D EA/D+BY EA/RS EA/SD EA/SD: EA/GS EA/GS+SRC EA/DM EA/WR EA+LPB EA+LPB: EA+DSCT EA+BCEP: EA+ZAND EA+ACYG EA+SRD EB/GS EB/DM EB/KE EB/KE: EW/KE EA/AR/RS EA/GS/D EA/D/WR").contains(varType))
			{
				varstartype = q_("eclipsing binary system");
				ebsFlag = true;
			}
			else
				varstartype = q_("variable star");
		}

		if (s->getComponentIds() || wdsObs>0)
			startype = q_("double star");
		else
			startype = q_("star");

		if (!varType.isEmpty())
		{
			if (s->getComponentIds() || wdsObs>0)
				oss << q_("Type: <b>%1, %2</b>").arg(varstartype).arg(startype);
			else
				oss << q_("Type: <b>%1</b>").arg(varstartype);
			oss << " (" << varType << ")<br />";
		} else
			oss << q_("Type: <b>%1</b>").arg(startype) << "<br />";

	}

	if (flags&Magnitude)
	{
		if (core->getSkyDrawer()->getFlagHasAtmosphere())
			oss << q_("Magnitude: <b>%1</b> (after extinction: <b>%2</b>)").arg(QString::number(getVMagnitude(core), 'f', 2))
				   .arg(QString::number(getVMagnitudeWithExtinction(core), 'f', 2)) << "<br>";
		else
			oss << q_("Magnitude: <b>%1</b>").arg(QString::number(getVMagnitude(core), 'f', 2)) << "<br>";
	}

	if ((flags&AbsoluteMagnitude) && s->getPlx ()&& !isNan(s->getPlx()) && !isInf(s->getPlx()))
		oss << q_("Absolute Magnitude: %1").arg(getVMagnitude(core)+5.*(1.+std::log10(0.00001*s->getPlx())), 0, 'f', 2) << "<br>";

	if (flags&Extra)
	{
		oss << q_("Color Index (B-V): <b>%1</b>").arg(QString::number(s->getBV(), 'f', 2)) << "<br>";
	}
	
	if (flags&Extra)
	{
		if (!varType.isEmpty())
		{
			float minimumM1 = minVMag;
			float minimumM2 = min2VMag;
			if (magFlag==1) // Amplitude
			{
				minimumM1 += maxVMag;
				minimumM2 += maxVMag;
			}

			if (maxVMag!=99.f) // seems it is not eruptive variable star
			{
				if (min2VMag==99.f)
					oss << q_("Magnitude range: <b>%1</b>%2<b>%3</b> (Photometric system: %4)").arg(QString::number(maxVMag, 'f', 2)).arg(QChar(0x00F7)).arg(QString::number(minimumM1, 'f', 2)).arg(photoVSys) << "<br />";
				else
					oss << q_("Magnitude range: <b>%1</b>%2<b>%3/%4</b> (Photometric system: %5)").arg(QString::number(maxVMag, 'f', 2)).arg(QChar(0x00F7)).arg(QString::number(minimumM1, 'f', 2)).arg(QString::number(minimumM2, 'f', 2)).arg(photoVSys) << "<br />";
			}
		}
	}

	oss << getPositionInfoString(core, flags);

	if ((flags&Distance) && s->getPlx ()&& !isNan(s->getPlx()) && !isInf(s->getPlx()))
	{
		//TRANSLATORS: Unit of measure for distance - Light Years
		oss << q_("Distance: %1 ly").arg((AU/(SPEED_OF_LIGHT*86400*365.25)) / (s->getPlx()*((0.00001/3600)*(M_PI/180))), 0, 'f', 2) << "<br>";
	}

	if (flags&Extra)
	{
		if (s->getSpInt())
			oss << q_("Spectral Type: %1").arg(StarMgr::convertToSpectralType(s->getSpInt())) << "<br />";

		if (s->getPlx())
			oss << q_("Parallax: %1\"").arg(0.00001*s->getPlx(), 0, 'f', 5) << "<br />";

		if (vPeriod>0)
			oss << q_("Period: %1 days").arg(vPeriod) << "<br />";

		if (vEpoch>0 && vPeriod>0)
		{
			// Calculate next minimum or maximum light
			double vsEpoch = 2400000+vEpoch;
			double npDate = vsEpoch + vPeriod * ::floor(1.0 + (core->getJDE() - vsEpoch)/vPeriod);
			QString nextDate = StelUtils::julianDayToISO8601String(npDate).replace("T", " ");
			if (ebsFlag)
				oss << q_("Next minimum light: %1 UTC").arg(nextDate) << "<br />";
			else
				oss << q_("Next maximum light: %1 UTC").arg(nextDate) << "<br />";

		}

		if (vMm>0)
		{
			if (ebsFlag)
				oss << q_("Duration of eclipse: %1%").arg(vMm) << "<br />";
			else
				oss << q_("Rising time: %1%").arg(vMm) << "<br />";
		}

		if (wdsObs>0)
		{
			oss << QString("%1 (%4): %2%3").arg(q_("Position angle")).arg(QString::number(wdsPA, 'f', 2)).arg(QChar(0x00B0)).arg(wdsObs) << "<br />";
			if (wdsSep>0.f) // A spectroscopic binary or not?
			{
				if (wdsSep>60.f) // A wide binary star?
					oss << QString("%1 (%4): %2\" (%3)").arg(q_("Separation")).arg(QString::number(wdsSep, 'f', 3)).arg(StelUtils::decDegToDmsStr(wdsSep/3600.f)).arg(wdsObs) << "<br />";
				else
					oss << QString("%1 (%3): %2\"").arg(q_("Separation")).arg(QString::number(wdsSep, 'f', 3)).arg(wdsObs) << "<br />";
			}
		}
	}

	StelObject::postProcessInfoString(str, flags);

	return str;
}

QVariantMap StarWrapper1::getInfoMap(const StelCore *core) const
{
	QVariantMap map = StelObject::getInfoMap(core);

	const QString varType = StarMgr::getGcvsVariabilityType(s->getHip());
	const int wdsObs = StarMgr::getWdsLastObservation(s->getHip());
	const float wdsPA = StarMgr::getWdsLastPositionAngle(s->getHip());
	const float wdsSep = StarMgr::getWdsLastSeparation(s->getHip());
	const double vPeriod = StarMgr::getGcvsPeriod(s->getHip());

	QString varstartype = "no";
	QString startype = "star";
	if(!varType.isEmpty())
	{
		if (QString("FU GCAS I IA IB IN INA INB INT IT IN(YY) IS ISA ISB RCB RS SDOR UV UVN WR").contains(varType))
			varstartype = "eruptive";
		else if (QString("ACYG BCEP BCEPS CEP CEP(B) CW CWA CWB DCEP DCEPS DSCT DSCTC GDOR L LB LC M PVTEL RPHS RR RR(B) RRAB RRC RV RVA RVB SR SRA SRB SRC SRD SXPHE ZZ ZZA ZZB").contains(varType))
			varstartype = "pulsating";
		else if (QString("ACV, ACVO, BY, ELL, FKCOM, PSR, SXARI").contains(varType))
			varstartype = "rotating";
		else if (QString("N NA NB NC NL NR SN SNI SNII UG UGSS UGSU UGZ ZAND").contains(varType))
			varstartype = "cataclysmic";
		else if (QString("E EA EB EW GS PN RS WD WR AR D DM DS DW K KE KW SD E: E:/WR E/D E+LPB: EA/D EA/D+BY EA/RS EA/SD EA/SD: EA/GS EA/GS+SRC EA/DM EA/WR EA+LPB EA+LPB: EA+DSCT EA+BCEP: EA+ZAND EA+ACYG EA+SRD EB/GS EB/DM EB/KE EB/KE: EW/KE EA/AR/RS EA/GS/D EA/D/WR").contains(varType))
		{
			varstartype = "eclipsing-binary";
		}
		else
			varstartype = "variable";
	}
	map.insert("variable-star", varstartype);

	if (s->getComponentIds() || wdsObs>0)
		startype = "double-star";

	map.insert("star-type", startype);

	map.insert("bV", s->getBV());

	if (s->getPlx ()&& !isNan(s->getPlx()) && !isInf(s->getPlx()))
	{
		map.insert("parallax", 0.00001*s->getPlx());
		map.insert("absolute-mag", getVMagnitude(core)+5.*(1.+std::log10(0.00001*s->getPlx())));
		map.insert("distance-ly", (AU/(SPEED_OF_LIGHT*86400*365.25)) / (s->getPlx()*((0.00001/3600)*(M_PI/180))));
	}

	if (s->getSpInt())
		map.insert("spectral-class", StarMgr::convertToSpectralType(s->getSpInt()));

	if (vPeriod>0)
		map.insert("period", vPeriod);

	if (wdsObs>0)
	{
		map.insert("wds-year", wdsObs);
		map.insert("wds-position-angle", wdsPA);
		map.insert("wds-separation", wdsSep);
	}

	return map;
}

StelObjectP Star1::createStelObject(const SpecialZoneArray<Star1> *a,
									const SpecialZoneData<Star1> *z) const {
  return StelObjectP(new StarWrapper1(a,z,this), true);
}

StelObjectP Star2::createStelObject(const SpecialZoneArray<Star2> *a,
									const SpecialZoneData<Star2> *z) const {
  return StelObjectP(new StarWrapper2(a,z,this), true);
}

StelObjectP Star3::createStelObject(const SpecialZoneArray<Star3> *a,
									const SpecialZoneData<Star3> *z) const {
  return StelObjectP(new StarWrapper3(a,z,this), true);
}

