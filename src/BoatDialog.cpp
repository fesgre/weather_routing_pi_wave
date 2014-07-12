/***************************************************************************
 *
 * Project:  OpenCPN Weather Routing plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2014 by Sean D'Epagnier                                 *
 *   sean@depagnier.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 *
 */

#include <wx/wx.h>

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "weather_routing_pi.h"
#include "Utilities.h"
#include "Boat.h"
#include "SwitchPlanDialog.h"
#include "BoatDialog.h"

enum {spNAME, spETA};

// for plotting
static const int wind_speeds[] = {0, 2, 4, 6, 8, 10, 12, 15, 18, 21, 24, 28, 32, 36, 40, 45, 50, 55, 60};
static const int num_wind_speeds = (sizeof wind_speeds) / (sizeof *wind_speeds);

BoatDialog::BoatDialog(wxWindow *parent, wxString boatpath)
    : BoatDialogBase(parent), m_boatpath(boatpath), m_PlotScale(0)
{
    m_Boat.OpenXML(m_boatpath);
    m_SelectedSailPlan = 0;

    m_lBoatPlans->InsertColumn(spNAME, _("Name"));
    m_lBoatPlans->InsertColumn(spETA, _("Eta"));
    RepopulatePlans();

    m_SelectedSailPlan = 0;
    BoatPlan &curplan = m_Boat.Plans[m_SelectedSailPlan];

    if(curplan.computed) {
        m_sDisplacement->SetValue(m_Boat.displacement_tons);
        m_sLWL->SetValue(m_Boat.lwl_ft);
        m_sLOA->SetValue(m_Boat.loa_ft);

        m_sFrictionalDrag->SetValue(m_Boat.frictional_drag * 1000.0);
        m_sWakeDrag->SetValue(m_Boat.wake_drag * 100.0);

        m_cbWingWingRunning->SetValue(curplan.wing_wing_running);
        m_cbOptimizeTacking->SetValue(curplan.optimize_tacking);

        SetEta(curplan.eta);
    } else
        m_rbCSV->SetValue(true);

    m_lBoatPlans->SetItemState(m_SelectedSailPlan, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

    UpdateVMG();
    UpdateStats();

    Fit();

    wxFileConfig *pConf = GetOCPNConfigObject();
    pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );
}

BoatDialog::~BoatDialog()
{
}

void BoatDialog::OnMouseEventsPlot( wxMouseEvent& event )
{
    if(event.Leaving()) {
        m_stTrueWindAngle->SetLabel(_("N/A"));
        m_stTrueWindKnots->SetLabel(_("N/A"));
        m_stApparentWindAngle->SetLabel(_("N/A"));
        m_stApparentWindKnots->SetLabel(_("N/A"));
        m_stBoatAngle->SetLabel(_("N/A"));
        m_stBoatKnots->SetLabel(_("N/A"));
    }

    wxPoint p = event.GetPosition();
    int w, h;
    m_PlotWindow->GetSize( &w, &h);

    /* range + to - */
    double W, VW, B, VB, A, VA;
    double windspeed;

    switch(m_lPlotType->GetSelection()) {
    case 0:
        if(m_cPlotType->GetSelection() == 0) { // polar
            if(!m_PlotScale)
                return;
            
            double x = (double)p.x - w/2;
            double y = (double)p.y - h/2;
            
            /* range +- */
            x /= m_PlotScale;
            y /= m_PlotScale;
            
            B = rad2posdeg(atan2(x, -y));
        } else
            B = (double)p.x/w*360;
        
        windspeed = m_sWindSpeed->GetValue();
        break;
    case 1:
    {
        B = m_sWindDirection->GetValue();
        double i = (double)p.x/w*num_wind_speeds;
        int i0 = floor(i), i1 = ceil(i);
        double d = i - i0;
        windspeed = (1-d)*wind_speeds[i0] + d*wind_speeds[i1];
    } break;
    }

    switch(m_cPlotVariable->GetSelection()) {
    case 0: // true wind
        W = B;
        VW = windspeed;
        VB = m_Boat.Plans[m_SelectedSailPlan].Speed(W, VW);

        VA = BoatPlan::VelocityApparentWind(VB, deg2rad(W), VW);
        A = rad2posdeg(BoatPlan::DirectionApparentWind(VA, VB, deg2rad(W), VW));
        break;
    case 1:
        A = heading_resolve(B);
        VW = windspeed;
        VB = m_Boat.Plans[m_SelectedSailPlan].SpeedAtApparentWindDirection(A, VW, &W);
        W = positive_degrees(W);

        VA = BoatPlan::VelocityApparentWind(VB, deg2rad(W), VW);
        break;
    case 2:
        W = B;
        VA = windspeed;
        VB = m_Boat.Plans[m_SelectedSailPlan].SpeedAtApparentWindSpeed(W, VA);
        VW = BoatPlan::VelocityTrueWind(VA, VB, deg2rad(W));
        A = rad2posdeg(BoatPlan::DirectionApparentWind(VA, VB, deg2rad(W), VW));
        break;
    case 3:
        A = heading_resolve(B);
        VA = windspeed;
        VB = m_Boat.Plans[m_SelectedSailPlan].SpeedAtApparentWind(A, VA, &W);
        W = positive_degrees(W);
        VW = BoatPlan::VelocityTrueWind(VA, VB, deg2rad(W));
    }

    m_stBoatAngle->SetLabel(wxString::Format(_T("%03.0f"), B));
    m_stBoatKnots->SetLabel(wxString::Format(_T("%.1f"), VB));

    int newmousew = round(B);
    if(newmousew != m_MouseW) {
        m_MouseW = newmousew;
        m_PlotWindow->Refresh();
    }
    
    m_stTrueWindAngle->SetLabel(wxString::Format(_T("%03.0f"), W));
    m_stTrueWindKnots->SetLabel(wxString::Format(_T("%.1f"), VW));

    m_stApparentWindAngle->SetLabel(wxString::Format(_T("%03.0f"), A));

    m_stApparentWindKnots->SetLabel(wxString::Format(_T("%.1f"), VA));
}

void BoatDialog::PlotVMG(wxPaintDC &dc, double W, double s)
{
    if(isnan(W))
        return;

    int w, h;
    m_PlotWindow->GetSize( &w, &h);

    double H;
    if((m_cPlotVariable->GetSelection() & 1) == 0) // true wind
        H = W;
    else { // apparent wind
        BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
        int VW = m_sWindSpeed->GetValue();
        H = rad2posdeg(BoatPlan::DirectionApparentWind(plan.Speed(W, VW), deg2rad(W), VW));
    }

    if(m_cPlotType->GetSelection() == 0) { // polar
        int px = s*sin(deg2rad(H));
        int py = s*cos(deg2rad(H));
        dc.DrawLine(w/2, h/2, w/2 + px, h/2 - py);
    } else {
        int px = H * w / DEGREES;
        dc.DrawLine(px, 0, px, h);
    }
}

static void PlotPolarData(wxDC &dc, double *values, int count, double scale, int w, int h)
{
    int lx, ly;
    bool lastvalid = false;
    for(int i = 0; i<=count; i++) {
        int j = i%count;
        if(isnan(values[j])) {
            lastvalid = false;
            continue;
        }

        int px =  scale*values[j]*sin(deg2rad(i)) + w/2;
        int py = -scale*values[j]*cos(deg2rad(i)) + h/2;

        if(lastvalid)
            dc.DrawLine(lx, ly, px, py);

        lx = px, ly = py;
        lastvalid = true;
    }
}

static void PlotRectangularData(wxDC &dc, double *values, int count, double scale, int w, int h)
{
    int lx, ly;
    bool lastvalid = false;
    for(int i = 0; i<count; i++) {
        if(isnan(values[i])) {
            lastvalid = false;
            continue;
        }

        int px = i * w / count, py = h - scale*values[i];

        if(lastvalid)
            dc.DrawLine(lx, ly, px, py);

        lx = px, ly = py;
        lastvalid = true;
    }
}

void BoatDialog::PaintPolar(wxPaintDC &dc)
{
    int w, h;
    m_PlotWindow->GetSize( &w, &h);
    double maxVB = 0;

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
    int windspeed = m_sWindSpeed->GetValue();

    int H, i;
    /* plot scale */
    int selection = m_cPlotVariable->GetSelection();

    for(H = 0, i=0; H<DEGREES; H += 3, i++) {
        double VB;
        if(selection < 2)
            VB = plan.Speed(H, windspeed);
        else
            VB = plan.SpeedAtApparentWindSpeed(H, windspeed);

        if(VB > maxVB)
            maxVB = VB;
    }
    
    dc.SetPen(wxPen(wxColor(0, 0, 0)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetTextForeground(wxColour(0, 55, 75));

    if(maxVB <= 0) maxVB = 1; /* avoid lock */
    double Vstep = ceil(maxVB / 5);
    maxVB += Vstep;

    if(m_cPlotType->GetSelection() == 0) { // polar
        m_PlotScale = (w < h ? w : h)/1.8 / (maxVB+1);

        /* polar circles */
        for(double V = Vstep; V <= maxVB; V+=Vstep) {
            dc.DrawCircle(w/2, h/2, V*m_PlotScale);
            dc.DrawText(wxString::Format(_T("%.0f"), V), w/2, h/2+(int)V*m_PlotScale);
        }

        /* polar meridians */
        dc.SetTextForeground(wxColour(0, 0, 155));
        for(double B = 0; B < DEGREES; B+=15) {
            double x = maxVB*m_PlotScale*sin(deg2rad(B));
            double y = maxVB*m_PlotScale*cos(deg2rad(B));
            if(B < 180)
                dc.DrawLine(w/2 - x, h/2 + y, w/2 + x, h/2 - y);

            wxString str = wxString::Format(_T("%.0f"), B);
            int sw, sh;
            dc.GetTextExtent(str, &sw, &sh);
            dc.DrawText(str, w/2 + .9*x - sw/2, h/2 - .9*y - sh/2);
        }
    } else { // rectangular
        m_PlotScale = h/1.8 / (maxVB+1);

        for(double V = Vstep; V <= maxVB; V+=Vstep) {
            int y = h - 2*V*m_PlotScale;
            dc.DrawLine(0, y, w, y);
            dc.DrawText(wxString::Format(_T("%.0f"), V), 0, y);
        }

        dc.SetTextForeground(wxColour(0, 0, 155));
        for(double B = 0; B < DEGREES; B+=30) {
            double x = B * w / DEGREES;
            dc.DrawLine(x, 0, x, h);

            wxString str = wxString::Format(_T("%.0f"), B);
            int sw, sh;
            dc.GetTextExtent(str, &sw, &sh);
            dc.DrawText(str, x, 0);
        }
    }

    /* vmg */
    double s = maxVB*m_PlotScale;
    SailingVMG vmg = selection < 2 ? plan.GetVMGTrueWind(windspeed)
                                 : plan.GetVMGApparentWind(windspeed);

    dc.SetPen(wxPen(wxColor(255, 0, 255), 2));

    PlotVMG(dc, vmg.values[SailingVMG::PORT_UPWIND], s);
    PlotVMG(dc, vmg.values[SailingVMG::STARBOARD_UPWIND], s);

    dc.SetPen(wxPen(wxColor(255, 255, 0), 2));

    PlotVMG(dc, vmg.values[SailingVMG::PORT_DOWNWIND], s);
    PlotVMG(dc, vmg.values[SailingVMG::STARBOARD_DOWNWIND], s);

    /* boat speeds */
    double values[DEGREES];
    for(int H = 0; H<=DEGREES; H++) {
        switch(selection) {
        case 0: values[H] = plan.Speed(H, windspeed); break;
        case 1: values[H] = plan.SpeedAtApparentWindDirection(H, windspeed); break;
        case 2: values[H] = plan.SpeedAtApparentWindSpeed(H, windspeed); break;
        case 3: values[H] = plan.SpeedAtApparentWind(H, windspeed); break;
        }
    }

    dc.SetPen(wxPen(wxColor(255, 0, 0), 2));
    if(m_cPlotType->GetSelection() == 0)
        PlotPolarData(dc, values, DEGREES, m_PlotScale, w, h);
    else
        PlotRectangularData(dc, values, DEGREES, 2*m_PlotScale, w, h);

#if 0
        if(W == m_MouseW) {
            int B = speed.b;
            SailingSpeed speedB = plan->speed[VW][B];
            double BVB = speedB.VB;
            int pxb, pyb;
            if(m_cPlotType->GetValue() == 0) { // polar
                pxb =  m_PlotScale*speed.w*BVB*sin(deg2rad(B)) + w/2;
                pyb = -m_PlotScale*speed.w*BVB*cos(deg2rad(B)) + h/2;
            } else {

            }

            dc.SetPen(wxPen(wxColor(0, 0, 255), 2));
            dc.DrawLine(w/2, h/2, pxb, pyb);

            if(speed.w < 1) {
                dc.SetPen(wxPen(wxColor(0, 255, 0), 2));
                dc.DrawLine(pxb, pyb, px, py);
            }
        }
#endif
}

void BoatDialog::PaintSpeed(wxPaintDC &dc)
{
    int w, h;
    m_PlotWindow->GetSize( &w, &h);
    double maxVB = 0;

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
    int H = m_sWindDirection->GetValue();

    /* plot scale */
    int selection = m_cPlotVariable->GetSelection();

    for(int s = 0; s<num_wind_speeds; s++) {
        double windspeed = wind_speeds[s], VB;

        if(selection < 2)
            VB = plan.Speed(H, windspeed);
        else
            VB = plan.SpeedAtApparentWindSpeed(H, windspeed);

        if(VB > maxVB)
            maxVB = VB;
    }
    
    dc.SetPen(wxPen(wxColor(0, 0, 0)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetTextForeground(wxColour(0, 55, 75));

    if(maxVB <= 0) maxVB = 1; /* avoid lock */
    double Vstep = ceil(maxVB / 5);
    maxVB += Vstep;

    m_PlotScale = h/1.8 / (maxVB+1);

    for(double V = Vstep; V <= maxVB; V+=Vstep) {
        int y = h - 2*V*m_PlotScale;
        dc.DrawLine(0, y, w, y);
        dc.DrawText(wxString::Format(_T("%.0f"), V), 0, y);
    }

    dc.SetTextForeground(wxColour(0, 0, 155));

    for(int s = 0; s<num_wind_speeds; s++) {
        double windspeed = wind_speeds[s];

        double x = s * w / num_wind_speeds;
        dc.DrawLine(x, 0, x, h);

        wxString str = wxString::Format(_T("%.0f"), windspeed);
        int sw, sh;
        dc.GetTextExtent(str, &sw, &sh);
        dc.DrawText(str, x, 0);
    }

    /* boat speeds */
    double *values = new double[num_wind_speeds];
    for(int s = 0; s<num_wind_speeds; s++) {
        double windspeed = wind_speeds[s];
        switch(selection) {
        case 0: values[s] = plan.Speed(H, windspeed); break;
        case 1: values[s] = plan.SpeedAtApparentWindSpeed(H, windspeed); break;
        case 2: values[s] = plan.SpeedAtApparentWindDirection(H, windspeed); break;
        case 3: values[s] = plan.SpeedAtApparentWind(H, windspeed); break;
        }
    }

    dc.SetPen(wxPen(wxColor(255, 0, 0), 2));
    PlotRectangularData(dc, values, num_wind_speeds, 2*m_PlotScale, w, h);
    delete [] values;
}

void BoatDialog::PaintVMG(wxPaintDC &dc)
{
    int w, h;
    m_PlotWindow->GetSize( &w, &h);

    m_PlotScale = 0; // don't do mouse over

    int mul = 5, count = mul*num_wind_speeds - 1;

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];

    /* plot scale */
    int selection = m_cPlotVariable->GetSelection();

    double min = 0, max = 190, step = 10;
    
    dc.SetPen(wxPen(wxColor(0, 0, 0)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetTextForeground(wxColour(0, 55, 75));

    double scale = h/(max - min);

    /* draw angles lines and text */
    for(double V = step; V < max; V+=step) {
        int y = h - V*scale;
        dc.DrawLine(0, y, w, y);
        dc.DrawText(wxString::Format(_T("%.0f"), V), 0, y);
    }

    /* draw wind speeds and lines and text */
    dc.SetTextForeground(wxColour(0, 0, 155));
    for(int s = 0; s<num_wind_speeds; s++) {
        double windspeed = wind_speeds[s];

        double x = s * w / num_wind_speeds;
        dc.DrawLine(x, 0, x, h);

        wxString str = wxString::Format(_T("%.0f"), windspeed);
        int sw, sh;
        dc.GetTextExtent(str, &sw, &sh);
        dc.DrawText(str, x, 0);
    }

    // Only render port tack.. later we could do starboard if the polar is not symmetric
    double *valuesup = new double[count], *valuesdown = new double[count];
    for(int s = 0; s<count; s++) {
        int s0 = s/mul;
        double d = (double)s/mul - s0, v;
        double windspeed1 = wind_speeds[s0];
        double windspeed2 = wind_speeds[s0+1];
        double windspeed = (1-d)*windspeed1 + d*windspeed2;

        SailingVMG vmg = selection < 2 ? plan.GetVMGTrueWind(windspeed) :
            plan.GetVMGApparentWind(windspeed);

        v = vmg.values[SailingVMG::PORT_UPWIND];
        if(selection & 1 && !isnan(v))
            v = rad2posdeg(BoatPlan::DirectionApparentWind
                                   (plan.Speed(v, windspeed), deg2rad(v), windspeed));
        valuesup[s] = v;

        v = vmg.values[SailingVMG::PORT_DOWNWIND];
        if(selection & 1 && !isnan(v))
            v = rad2posdeg(BoatPlan::DirectionApparentWind
                                   (plan.Speed(v, windspeed), deg2rad(v), windspeed));
        valuesdown[s] = v;
    }

    dc.SetPen(wxPen(wxColor(255, 0, 255), 2));
    PlotRectangularData(dc, valuesup, count, scale, w, h);

    dc.SetPen(wxPen(wxColor(255, 255, 0), 2));
    PlotRectangularData(dc, valuesdown, count, scale, w, h);
    delete [] valuesup;
    delete [] valuesdown;
}

void BoatDialog::OnPaintPlot(wxPaintEvent& event)
{
    wxWindow *window = dynamic_cast<wxWindow*>(event.GetEventObject());
    if(!window)
        return;

    wxPaintDC dc(window);
    dc.SetBackgroundMode(wxTRANSPARENT);

    switch(m_lPlotType->GetSelection()) {
    case 0: PaintPolar(dc); break;
    case 1: PaintSpeed(dc); break;
    case 2: PaintVMG(dc); break;
    }
}

void BoatDialog::OnOpen ( wxCommandEvent& event )
{
    wxFileConfig *pConf = GetOCPNConfigObject();
    pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );

    wxString path;
    pConf->Read ( _T ( "Path" ), &path, weather_routing_pi::StandardPath());

    wxFileDialog openDialog
        ( this, _( "Select Polar" ), path, wxT ( "" ),
          wxT ( "Boat polar files (*.xml)|*.XML;*.xml|All files (*.*)|*.*" ),
          wxFD_OPEN  );

    if( openDialog.ShowModal() == wxID_OK ) {
        wxString filename = openDialog.GetPath();
        pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );
        pConf->Write ( _T ( "Path" ), wxFileName(filename).GetPath() );

        wxString error = m_Boat.OpenXML(filename);
        if(error.empty())
            RepopulatePlans();
        else {
            wxMessageDialog md(this, error, _("OpenCPN Weather Routing Plugin"),
                               wxICON_ERROR | wxOK );
            md.ShowModal();
            return;
        }

        UpdateStats();
        UpdateVMG();
        m_PlotWindow->Refresh();
    }
}

void BoatDialog::LoadCSV(bool switched)
{
    wxString filename = m_fpCSVPath->GetPath();
    BoatPlan &plan = m_Boat.Plan(m_SelectedSailPlan);
    if(plan.Open(filename.mb_str()))
        RepopulatePlans();
    else {
        if(switched) {
            wxFileDialog openDialog
                ( this, _( "Select Polar CSV" ), m_fpCSVPath->GetPath(), wxT ( "" ),
                  wxT ( "CSV (*.csv)|*.CSV;*.csv;*.csv.gz;*.csv.bz2|All files (*.*)|*.*" ),
                  wxFD_OPEN  );

            if( openDialog.ShowModal() == wxID_OK ) {
                m_fpCSVPath->SetPath(openDialog.GetPath());
                LoadCSV(false);
            } else
                m_rbComputed->SetValue(true);
        } else {
//            wxMessageDialog md(this, _("Failed reading csv: ") + filename,
//                               _("OpenCPN Weather Routing Plugin"),
//                               wxICON_ERROR | wxOK );
//            md.ShowModal();
        }
        return;
    }
    
    UpdateStats();
    UpdateVMG();
    m_PlotWindow->Refresh();
}

void BoatDialog::Save()
{
    wxString error = m_Boat.SaveXML(m_boatpath);
    if(error.empty())
        EndModal(wxID_OK);
    else {
        wxMessageDialog md(this, error, _("OpenCPN Weather Routing Plugin"),
                           wxICON_ERROR | wxOK );
        md.ShowModal();
    }
}

void BoatDialog::OnSaveAs ( wxCommandEvent& event )
{
    wxFileConfig *pConf = GetOCPNConfigObject();
    pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );

    wxString path;
    pConf->Read ( _T ( "Path" ), &path, weather_routing_pi::StandardPath());

    wxFileDialog saveDialog( this, _( "Select Polar" ), path, wxT ( "" ),
                             wxT ( "Boat files (*.xml)|*.XML;*.xml|All files (*.*)|*.*" ),
                             wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( saveDialog.ShowModal() == wxID_OK ) {
        wxString filename = wxFileDialog::AppendExtension(saveDialog.GetPath(), _T("*.xml"));

        pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );
        pConf->Write ( _T ( "Path" ), wxFileName(filename).GetPath() );

        m_boatpath = filename;
        Save();
    }
}

void BoatDialog::OnClose ( wxCommandEvent& event )
{
    EndModal(wxID_CANCEL);
}

void BoatDialog::OnSaveCSV ( wxCommandEvent& event )
{
    wxFileConfig *pConf = GetOCPNConfigObject();
    pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );

    wxString path;
    pConf->Read ( _T ( "CSVPath" ), &path, weather_routing_pi::StandardPath());

    wxFileDialog saveDialog( this, _( "Select Polar" ), path, wxT ( "" ),
                             wxT ( "Boat Polar files (*.csv)|*.CSV;*.csv|All files (*.*)|*.*" ), wxFD_SAVE  );

    if( saveDialog.ShowModal() == wxID_OK ) {
        wxString filename = saveDialog.GetPath();
        pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );
        pConf->Write ( _T ( "CSVPath" ), wxFileName(filename).GetPath() );

        BoatPlan &plan = m_Boat.Plan(m_SelectedSailPlan);
        plan.ComputeBoatSpeeds(m_Boat, -1);
        if(!plan.Save(saveDialog.GetPath().mb_str())) {
            wxMessageDialog md(this, _("Failed saving boat polar to csv"), _("OpenCPN Weather Routing Plugin"),
                               wxICON_ERROR | wxOK );
            md.ShowModal();
        }
    }
}

void BoatDialog::OnPolarCSVFile( wxFileDirPickerEvent& event )
{
    LoadCSV();
}

void BoatDialog::OnRecompute()
{
    StoreBoatParameters();
    Compute();
    UpdateStats();
}

void BoatDialog::OnUpdatePlot()
{
    if(m_Boat.Plans[m_SelectedSailPlan].computed)
        OnRecompute();
    else
        m_PlotWindow->Refresh();
}

void BoatDialog::OnOptimizeTacking ( wxCommandEvent& event )
{
    if(event.IsChecked())
        m_Boat.Plans[m_SelectedSailPlan].OptimizeTackingSpeed();
    else
        m_Boat.Plans[m_SelectedSailPlan].ResetOptimalTackingSpeed();

    StoreBoatParameters();
    m_PlotWindow->Refresh();
}

void BoatDialog::OnRecomputeDrag( wxCommandEvent& event )
{
    StoreBoatParameters();
    m_Boat.RecomputeDrag();
    m_sFrictionalDrag->SetValue(1000.0 * m_Boat.frictional_drag);
    m_sWakeDrag->SetValue(100.0 * m_Boat.wake_drag);
    Compute();
}

void BoatDialog::OnDragInfo( wxCommandEvent& event )
{
    wxMessageDialog md(this, _("\
Drag is both frictional and wave based\n\
This can be computed based on boat values above then modified manually.\n\
With proper settings hull speed will be achieved, but also break into planing mode possible.\n\
A value 0 zero means no wake, where 100 is heaviest displacement.\n\
This also takes into account the different harmonics of wakes before hull speed is reached, \
so resulting boat polar may appear bumpy.\n"),
                       _("Drag Computation"),
                       wxICON_INFORMATION | wxOK );
    md.ShowModal();
}

void BoatDialog::OnSailPlanSelected( wxListEvent& event )
{
    m_SelectedSailPlan = event.GetIndex();
    
    BoatPlan &curplan = m_Boat.Plans[m_SelectedSailPlan];
    bool c = curplan.computed;

    if(c) {
        m_sLuffAngle->SetValue(curplan.luff_angle);
        m_cbOptimizeTacking->SetValue(curplan.optimize_tacking);
        m_cbWingWingRunning->SetValue(curplan.wing_wing_running);
        double eta = curplan.eta;
        SetEta(eta);
        m_sEta->SetValue(sqrt(eta) * 1000.0);
    }

    m_sbComputation->ShowItems(c);
    m_sbCSV->ShowItems(!c);
    m_pPolarConfig->Fit();
    Fit();

    m_PlotWindow->Refresh();
}

void BoatDialog::OnPolarMode( wxCommandEvent& event )
{
    bool c = m_rbComputed->GetValue();

    m_Boat.Plans[m_SelectedSailPlan].computed = c;
    m_sbComputation->ShowItems(c);
    m_sbCSV->ShowItems(!c);

    if(c)
        OnRecompute();
    else {
        LoadCSV(true);
    }

    m_pPolarConfig->Fit();
    Fit();
}

void BoatDialog::OnEtaSlider( wxScrollEvent& event )
{
    SetEta(pow(m_sEta->GetValue() / 1000.0, 2));
    StoreBoatParameters();
    Compute();
}

void BoatDialog::OnEta( wxCommandEvent& event )
{
    StoreBoatParameters();
    Compute();
}

void BoatDialog::OnNewBoatPlan( wxCommandEvent& event )
{
    wxString np = _("New Plan");
    m_SelectedSailPlan = m_lBoatPlans->InsertItem(m_lBoatPlans->GetItemCount(), np);
    m_Boat.Plans.push_back(BoatPlan(np, m_Boat));
    m_lBoatPlans->SetItemState(m_SelectedSailPlan, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    StoreBoatParameters();
    Compute();
    m_bDeleteBoatPlan->Enable();

    wxFileConfig *pConf = GetOCPNConfigObject();
    pConf->SetPath ( _T( "/PlugIns/WeatherRouting/BoatDialog" ) );

    wxString path;
    pConf->Read ( _T ( "CSVPath" ), &path, weather_routing_pi::StandardPath());
    m_fpCSVPath->SetPath(path);
}

void BoatDialog::OnDeleteBoatPlan( wxCommandEvent& event )
{
    m_lBoatPlans->DeleteItem(m_SelectedSailPlan);
    m_Boat.Plans.erase(m_Boat.Plans.begin() + m_SelectedSailPlan);

    if(m_Boat.Plans.size() < 2)
        m_bDeleteBoatPlan->Disable();
}

void BoatDialog::StoreBoatParameters()
{
    if(m_SelectedSailPlan < 0 ||
       m_SelectedSailPlan >= (int)m_Boat.Plans.size())
        return;

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
    m_tEta->GetValue().ToDouble(&plan.eta);
    plan.luff_angle = m_sLuffAngle->GetValue();
    plan.optimize_tacking = m_cbOptimizeTacking->GetValue();
    plan.wing_wing_running = m_cbWingWingRunning->GetValue();

    m_Boat.hulltype = (Boat::HullType)m_cHullType->GetSelection();

    m_Boat.displacement_tons = m_sDisplacement->GetValue();
    m_Boat.lwl_ft = m_sLWL->GetValue();
    m_Boat.loa_ft = m_sLOA->GetValue();
    m_Boat.beam_ft = m_sBeam->GetValue();

    m_Boat.frictional_drag = m_sFrictionalDrag->GetValue() / 1000.0;
    m_Boat.wake_drag = m_sWakeDrag->GetValue() / 100.0;
}

void BoatDialog::RepopulatePlans()
{
    m_lBoatPlans->DeleteAllItems();

    if(m_Boat.Plans.size() == 0) {
        m_Boat.Plans.push_back(BoatPlan(_("Initial Plan"), m_Boat));
        m_Boat.Plans[0].ComputeBoatSpeeds(m_Boat, m_sWindSpeed->GetValue());
    }

    for(unsigned int i=0; i<m_Boat.Plans.size(); i++) {
        wxListItem info;
        info.SetId(i);
        info.SetData(i);
        long idx = m_lBoatPlans->InsertItem(info);
        BoatPlan &plan = m_Boat.Plans[i];
        m_lBoatPlans->SetItem(idx, spNAME, plan.Name);
        m_lBoatPlans->SetItem(idx, spETA, wxString::Format(_T("%.2f"), plan.eta));
    }

    m_lBoatPlans->SetColumnWidth(spNAME, 80);
    m_lBoatPlans->SetColumnWidth(spETA, 50);

    if(m_Boat.Plans.size() > 1)
        m_bDeleteBoatPlan->Enable();
    else
        m_bDeleteBoatPlan->Disable();

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];

    m_fpCSVPath->SetPath(plan.csvFileName);
    m_stWindSpeedStep->SetLabel
        (wxString::Format(_T("%d"), plan.wind_speed_step));
    m_stWindDegreeStep->SetLabel
        (wxString::Format(_T("%d"), plan.wind_degree_step));
}

void BoatDialog::Compute()
{
    if(m_SelectedSailPlan < 0 ||
       m_SelectedSailPlan >= (int)m_Boat.Plans.size())
        return;

    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];

    int selection = m_cPlotVariable->GetSelection();

    plan.ComputeBoatSpeeds(m_Boat, (selection < 2 && m_lPlotType->GetSelection() == 0)
                           ? m_sWindSpeed->GetValue() : -1);

    UpdateVMG();

    if(m_cbOptimizeTacking->IsChecked())
        m_Boat.Plans[m_SelectedSailPlan].OptimizeTackingSpeed();

    m_PlotWindow->Refresh();
    m_lBoatPlans->SetItem(m_SelectedSailPlan, spETA, wxString::Format(_T("%.2f"), plan.eta));
}

wxString BoatDialog::FormatVMG(double W, double VW)
{
    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
    double A = isnan(W) ? NAN :
        rad2posdeg(BoatPlan::DirectionApparentWind(plan.Speed(W, VW), deg2rad(W), VW));
    return wxString::Format(_T("%.1f True %.1f Apparent"), W, A);
}

void BoatDialog::UpdateVMG()
{ 
    int windspeed = m_sVMGWindSpeed->GetValue();
    BoatPlan &plan = m_Boat.Plans[m_SelectedSailPlan];
    SailingVMG vmg = m_cVMGTrueApparent->GetSelection() ?
        plan.GetVMGApparentWind(windspeed) :
        plan.GetVMGTrueWind(windspeed);

    m_stBestCourseUpWindPortTack->SetLabel(FormatVMG(vmg.values[SailingVMG::PORT_UPWIND], windspeed));
    m_stBestCourseUpWindStarboardTack->SetLabel(FormatVMG(vmg.values[SailingVMG::STARBOARD_UPWIND], windspeed));
    m_stBestCourseDownWindPortTack->SetLabel(FormatVMG(vmg.values[SailingVMG::PORT_DOWNWIND], windspeed));
    m_stBestCourseDownWindStarboardTack->SetLabel(FormatVMG(vmg.values[SailingVMG::STARBOARD_DOWNWIND], windspeed));
}

void BoatDialog::OnNewSwitchPlanRule( wxCommandEvent& event )
{
    SwitchPlan plan;
    
    plan.Name = m_Boat.Plans[0].Name;

    m_Boat.Plans[m_SelectedSailPlan].SwitchPlans.push_back(plan);

    int index = m_lSwitchPlans->Append(wxString());
    m_lSwitchPlans->SetSelection(index, true);

    m_bEditSwitchBoatPlan->Enable();
    m_bDeleteSwitchBoatPlan->Enable();

    OnEditSwitchPlanRule(event);
}

void BoatDialog::OnEditSwitchPlanRule( wxCommandEvent& event )
{
    int index = m_lSwitchPlans->GetSelection();
    if(index < 0)
        return;

    BoatPlan &boatplan = m_Boat.Plans[m_SelectedSailPlan];
    SwitchPlan plan = boatplan.SwitchPlans[index];

    if(m_Boat.Plans.size() < 1) {
        wxMessageDialog md(this, _("Cannot edit switch plan since there is no other plan to switch to."),
                           _("Sail Plans"),
                           wxICON_ERROR | wxOK );
        md.ShowModal();
        return;
    }

    SwitchPlanDialog dialog(this, plan, m_Boat.Plans);
    if(dialog.ShowModal() == wxID_OK)
        boatplan.SwitchPlans[index] = plan;
    else
        boatplan.SwitchPlans.erase(boatplan.SwitchPlans.begin() + index);

    PopulatePlans();
}

void BoatDialog::OnDeleteSwitchPlanRule( wxCommandEvent& event )
{
    int index = m_lSwitchPlans->GetSelection();
    if(index < 0)
        return;

    BoatPlan &boatplan = m_Boat.Plans[m_SelectedSailPlan];
    SwitchPlan plan = boatplan.SwitchPlans[index];
    boatplan.SwitchPlans.erase(boatplan.SwitchPlans.begin() + index);
    PopulatePlans();
}

void BoatDialog::PopulatePlans()
{
    BoatPlan &boatplan = m_Boat.Plans[m_SelectedSailPlan];

    if(boatplan.SwitchPlans.size() == 0) {
        m_bEditSwitchBoatPlan->Disable();
        m_bDeleteSwitchBoatPlan->Disable();
    }

    m_lSwitchPlans->Clear();

    for(unsigned int i=0; i<boatplan.SwitchPlans.size(); i++) {
        SwitchPlan plan = boatplan.SwitchPlans[i];
    
        wxString des, a, andstr = wxString(_T(" ")) + _("and") + wxString(_T(" "));
        if(!isnan(plan.MaxWindSpeed))
            des += a + _("Wind Speed > ") + wxString::Format(_T("%.0f"), plan.MaxWindSpeed), a = andstr;
        
        if(!isnan(plan.MinWindSpeed))
            des += a + _("Wind Speed < ") + wxString::Format(_T("%.0f"), plan.MinWindSpeed), a = andstr;

        if(!isnan(plan.MaxWindDirection))
            des += a + _("Wind Direction > ") + wxString::Format(_T("%.0f"), plan.MaxWindDirection), a = andstr;
        
        if(!isnan(plan.MinWindDirection))
            des += a + _("Wind Direction < ") + wxString::Format(_T("%.0f"), plan.MinWindDirection), a = andstr;
        
        if(!isnan(plan.MaxWaveHeight))
            des += a + _("Wave Height > ") + wxString::Format(_T("%.0f"), plan.MaxWaveHeight), a = andstr;

        if(!isnan(plan.MinWaveHeight))
            des += a + _("Wave Height < ") + wxString::Format(_T("%.0f"), plan.MinWaveHeight), a = andstr;

        if(!plan.DayTime) {
            des += a + _("Night Time");
        } else if(!plan.NightTime) {
            des += a + _("Day Time");
        }
        
        des += _(" switch to ");
        des += plan.Name;
        
        m_lSwitchPlans->Append(des);
    }
}
    
void BoatDialog::UpdateStats()
{
    m_stHullSpeed->SetLabel(wxString::Format(_T("%.3f"), m_Boat.HullSpeed()));
    m_stCapsizeRisk->SetLabel(wxString::Format(_T("%.3f"), m_Boat.CapsizeRisk()));
    m_stComfortFactor->SetLabel(wxString::Format(_T("%.3f"), m_Boat.ComfortFactor()));
    m_stDisplacementLengthRatio->SetLabel(wxString::Format(_T("%.3f"), m_Boat.DisplacementLengthRatio()));
}
