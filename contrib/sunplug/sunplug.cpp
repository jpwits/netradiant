/*
   Sunplug plugin for GtkRadiant
   Copyright (C) 2004 Topsun
   Thanks to SPoG for help!

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sunplug.h"
#include "globaldefs.h"

#include "debugging/debugging.h"

#include "iplugin.h"

#include "string/string.h"
#include "modulesystem/singletonmodule.h"

#include "iundo.h"       // declaration of undo system
#include "ientity.h"     // declaration of entity system
#include "iscenegraph.h" // declaration of datastructure of the map

#include "scenelib.h"    // declaration of datastructure of the map
#include "qerplugin.h"   // declaration to use other interfaces as a plugin

#include <gtk/gtk.h>     // to display something with gtk (windows, buttons etc.), the whole package might not be necessary

void about_plugin_window();

void MapCoordinator();

#if !GDEF_OS_WINDOWS

// linux itoa implementation
char *itoa(int value, char *result, int base)
{
    // check that the base if valid
    if (base < 2 || base > 16) {
        *result = 0;
        return result;
    }

    char *out = result;
    int quotient = value;

    do {
        *out = "0123456789abcdef"[abs(quotient % base)];
        ++out;

        quotient /= base;
    } while (quotient);

    // Only apply negative sign for base 10
    if (value < 0 && base == 10) {
        *out++ = '-';
    }

    std::reverse(result, out);

    *out = 0;
    return result;
}

#endif

typedef struct _mapcoord_setting_packet {
    ui::SpinButton spinner1{ui::null}, spinner2{ui::null}, spinner3{ui::null}, spinner4{ui::null};
    Entity *worldspawn;
} mapcoord_setting_packet;

static int map_minX, map_maxX, map_minY, map_maxY;
static int minX, maxX, minY, maxY;
mapcoord_setting_packet msp;

//  **************************
// ** find entities by class **  from radiant/map.cpp
//  **************************
class EntityFindByClassname : public scene::Graph::Walker {
    const char *m_name;
    Entity *&m_entity;
public:
    EntityFindByClassname(const char *name, Entity *&entity) : m_name(name), m_entity(entity)
    {
        m_entity = 0;
    }

    bool pre(const scene::Path &path, scene::Instance &instance) const
    {
        if (m_entity == 0) {
            Entity *entity = Node_getEntity(path.top());
            if (entity != 0
                && string_equal(m_name, entity->getKeyValue("classname"))) {
                m_entity = entity;
            }
        }
        return true;
    }
};

Entity *Scene_FindEntityByClass(const char *name)
{
    Entity *entity;
    GlobalSceneGraph().traverse(EntityFindByClassname(name, entity));
    return entity;
}

//  **************************
// ** GTK callback functions **
//  **************************

static gboolean delete_event(ui::Widget widget, GdkEvent *event, gpointer data)
{
    /* If you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal. Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit?'
     * type dialogs. */

    return FALSE;
}

// destroy widget if destroy signal is passed to widget
static void destroy(ui::Widget widget, gpointer data)
{
    widget.destroy();
}

// function for close button to destroy the toplevel widget
static void close_window(ui::Widget widget, gpointer data)
{
    widget.window().destroy();
}

// callback function to assign the optimal mapcoords to the spinboxes
static void input_optimal(ui::Widget widget, gpointer data)
{
    gtk_spin_button_set_value(msp.spinner1, minX);
    gtk_spin_button_set_value(msp.spinner2, maxY);
    gtk_spin_button_set_value(msp.spinner3, maxX);
    gtk_spin_button_set_value(msp.spinner4, minY);
}

// Spinner return value function
gint grab_int_value(ui::SpinButton a_spinner, gpointer user_data)
{
    return gtk_spin_button_get_value_as_int(a_spinner);
}

// write the values of the Spinner-Boxes to the worldspawn
static void set_coordinates(ui::Widget widget, gpointer data)
{
    //Str str_min, str_max;
    char buffer[10], str_min[20], str_max[20];

    itoa(gtk_spin_button_get_value_as_int(msp.spinner1), str_min, 10);
    itoa(gtk_spin_button_get_value_as_int(msp.spinner2), buffer, 10);
    strcat(str_min, " ");
    strcat(str_min, buffer);
    msp.worldspawn->setKeyValue("mapcoordsmins", str_min);

    itoa(gtk_spin_button_get_value_as_int(msp.spinner3), str_max, 10);
    itoa(gtk_spin_button_get_value_as_int(msp.spinner4), buffer, 10);
    strcat(str_max, " ");
    strcat(str_max, buffer);
    UndoableCommand undo("SunPlug.entitySetMapcoords");
    msp.worldspawn->setKeyValue("mapcoordsmaxs", str_max);

    close_window(widget, NULL);
}

class SunPlugPluginDependencies :
        public GlobalRadiantModuleRef,  // basic class for all other module refs
        public GlobalUndoModuleRef,     // used to say radiant that something has changed and to undo that
        public GlobalSceneGraphModuleRef, // necessary to handle data in the mapfile (change, retrieve data)
        public GlobalEntityModuleRef    // to access and modify the entities
{
public:
    SunPlugPluginDependencies() :
            GlobalEntityModuleRef(GlobalRadiant().getRequiredGameDescriptionKeyValue("entities"))
    { //,
    }
};

//  *************************
// ** standard plugin stuff **
//  *************************
namespace SunPlug {
    ui::Window main_window{ui::null};
    char MenuList[100] = "";

    const char *init(void *hApp, void *pMainWidget)
    {
        main_window = ui::Window::from(pMainWidget);
        return "Initializing SunPlug for GTKRadiant";
    }

    const char *getName()
    {
        return "SunPlug"; // name that is shown in the menue
    }

    const char *getCommandList()
    {
        const char about[] = "About...";
        const char etMapCoordinator[] = ";ET-MapCoordinator";

        strcat(MenuList, about);
        if (strncmp(GlobalRadiant().getGameName(), "etmain", 6) == 0) {
            strcat(MenuList, etMapCoordinator);
        }
        return (const char *) MenuList;
    }

    const char *getCommandTitleList()
    {
        return "";
    }

    void dispatch(const char *command, float *vMin, float *vMax, bool bSingleBrush)
    { // message processing
        if (string_equal(command, "About...")) {
            about_plugin_window();
        }
        if (string_equal(command, "ET-MapCoordinator")) {
            MapCoordinator();
        }
    }
} // namespace

class SunPlugModule : public TypeSystemRef {
    _QERPluginTable m_plugin;
public:
    typedef _QERPluginTable Type;

    STRING_CONSTANT(Name, "SunPlug");

    SunPlugModule()
    {
        m_plugin.m_pfnQERPlug_Init = &SunPlug::init;
        m_plugin.m_pfnQERPlug_GetName = &SunPlug::getName;
        m_plugin.m_pfnQERPlug_GetCommandList = &SunPlug::getCommandList;
        m_plugin.m_pfnQERPlug_GetCommandTitleList = &SunPlug::getCommandTitleList;
        m_plugin.m_pfnQERPlug_Dispatch = &SunPlug::dispatch;
    }

    _QERPluginTable *getTable()
    {
        return &m_plugin;
    }
};

typedef SingletonModule<SunPlugModule, SunPlugPluginDependencies> SingletonSunPlugModule;

SingletonSunPlugModule g_SunPlugModule;


extern "C" void RADIANT_DLLEXPORT Radiant_RegisterModules(ModuleServer &server)
{
    initialiseModule(server);

    g_SunPlugModule.selfRegister();
}

//  ************
// ** my stuff **
//  ************

// About dialog
void about_plugin_window()
{
    auto window = ui::Window(ui::window_type::TOP); // create a window
    gtk_window_set_transient_for(window, SunPlug::main_window); // make the window to stay in front of the main window
    window.connect("delete_event", G_CALLBACK(delete_event), NULL); // connect the delete event
    window.connect("destroy", G_CALLBACK(destroy), NULL); // connect the destroy event for the window
    gtk_window_set_title(window, "About SunPlug"); // set the title of the window for the window
    gtk_window_set_resizable(window, FALSE); // don't let the user resize the window
    gtk_window_set_modal(window, TRUE); // force the user not to do something with the other windows
    gtk_container_set_border_width(GTK_CONTAINER(window), 10); // set the border of the window

    auto vbox = ui::VBox(FALSE, 10); // create a box to arrange new objects vertically
    window.add(vbox);

    auto label = ui::Label("SunPlug v1.0 for NetRadiant 1.5\nby Topsun"); // create a label
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // text align left
    vbox.pack_start(label, FALSE, FALSE, 2); // insert the label in the box

    auto button = ui::Button("OK"); // create a button with text
    g_signal_connect_swapped(G_OBJECT(button), "clicked", G_CALLBACK(gtk_widget_destroy),
                             (void *) window); // connect the click event to close the window
    vbox.pack_start(button, FALSE, FALSE, 2); // insert the button in the box

    gtk_window_set_position(window, GTK_WIN_POS_CENTER); // center the window on screen

    gtk_widget_show_all(window); // show the window and all subelements
}

// get the current bounding box and return the optimal coordinates
void GetOptimalCoordinates(AABB *levelBoundingBox)
{
    int half_width, half_heigth, center_x, center_y;

    half_width = levelBoundingBox->extents.x();
    half_heigth = levelBoundingBox->extents.y();
    center_x = levelBoundingBox->origin.x();
    center_y = levelBoundingBox->origin.y();

    if (half_width > 175 || half_heigth > 175) { // the square must be at least 350x350 units
        // the wider side is the indicator for the square
        if (half_width >= half_heigth) {
            minX = center_x - half_width;
            maxX = center_x + half_width;
            minY = center_y - half_width;
            maxY = center_y + half_width;
        } else {
            minX = center_x - half_heigth;
            maxX = center_x + half_heigth;
            minY = center_y - half_heigth;
            maxY = center_y + half_heigth;
        }
    } else {
        minX = center_x - 175;
        maxX = center_x + 175;
        minY = center_y - 175;
        maxY = center_y + 175;
    }
}

// MapCoordinator dialog window
void MapCoordinator()
{
    Entity *theWorldspawn = NULL;
    const char *buffer;
    char line[20];

    // in any case we need a window to show the user what to do
    auto window = ui::Window(ui::window_type::TOP); // create the window
    gtk_window_set_transient_for(window, SunPlug::main_window); // make the window to stay in front of the main window
    window.connect("delete_event", G_CALLBACK(delete_event), NULL); // connect the delete event for the window
    window.connect("destroy", G_CALLBACK(destroy), NULL); // connect the destroy event for the window
    gtk_window_set_title(window, "ET-MapCoordinator"); // set the title of the window for the window
    gtk_window_set_resizable(window, FALSE); // don't let the user resize the window
    gtk_window_set_modal(window, TRUE); // force the user not to do something with the other windows
    gtk_container_set_border_width(GTK_CONTAINER(window), 10); // set the border of the window

    auto vbox = ui::VBox(FALSE, 10); // create a box to arrange new objects vertically
    window.add(vbox);

    scene::Path path = makeReference(GlobalSceneGraph().root()); // get the path to the root element of the graph
    scene::Instance *instance = GlobalSceneGraph().find(path); // find the instance to the given path
    AABB levelBoundingBox = instance->worldAABB(); // get the bounding box of the level

    theWorldspawn = Scene_FindEntityByClass("worldspawn"); // find the entity worldspawn
    if (theWorldspawn != 0) { // need to have a worldspawn otherwise setting a value crashes the radiant
        // next two if's: get the current values of the mapcoords
        buffer = theWorldspawn->getKeyValue("mapcoordsmins"); // upper left corner
        if (strlen(buffer) > 0) {
            strncpy(line, buffer, 19);
            map_minX = atoi(strtok(line, " ")); // minimum of x value
            map_minY = atoi(strtok(NULL, " ")); // maximum of y value
        } else {
            map_minX = 0;
            map_minY = 0;
        }
        buffer = theWorldspawn->getKeyValue("mapcoordsmaxs"); // lower right corner
        if (strlen(buffer) > 0) {
            strncpy(line, buffer, 19);
            map_maxX = atoi(strtok(line, " ")); // maximum of x value
            map_maxY = atoi(strtok(NULL, " ")); // minimum of y value
        } else {
            map_maxX = 0;
            map_maxY = 0;
        }

        globalOutputStream()
                << "SunPlug: calculating optimal coordinates\n"; // write to console that we are calculating the coordinates
        GetOptimalCoordinates(
                &levelBoundingBox); // calculate optimal mapcoords with the dimensions of the level bounding box
        globalOutputStream() << "SunPlug: adviced mapcoordsmins=" << minX << " " << maxY
                             << "\n"; // console info about mapcoordsmins
        globalOutputStream() << "SunPlug: adviced mapcoordsmaxs=" << maxX << " " << minY
                             << "\n"; // console info about mapcoordsmaxs

        auto spinner_adj_MinX = ui::Adjustment(map_minX, -65536.0, 65536.0, 1.0, 5.0,
                                               0); // create adjustment for value and range of minimum x value
        auto spinner_adj_MinY = ui::Adjustment(map_minY, -65536.0, 65536.0, 1.0, 5.0,
                                               0); // create adjustment for value and range of minimum y value
        auto spinner_adj_MaxX = ui::Adjustment(map_maxX, -65536.0, 65536.0, 1.0, 5.0,
                                               0); // create adjustment for value and range of maximum x value
        auto spinner_adj_MaxY = ui::Adjustment(map_maxY, -65536.0, 65536.0, 1.0, 5.0,
                                               0); // create adjustment for value and range of maximum y value

        auto button = ui::Button("Get optimal mapcoords"); // create button with text
        button.connect("clicked", G_CALLBACK(input_optimal), NULL); // connect button with callback function
        vbox.pack_start(button, FALSE, FALSE, 2); // insert button into vbox

        vbox.pack_start(ui::Widget::from(gtk_hseparator_new()), FALSE, FALSE, 2); // insert separator into vbox

        auto table = ui::Table(4, 3, TRUE); // create table
        gtk_table_set_row_spacings(table, 8); // set row spacings
        gtk_table_set_col_spacings(table, 8); // set column spacings
        vbox.pack_start(table, FALSE, FALSE, 2); // insert table into vbox

        auto label = ui::Label("x"); // create label
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // align text to the left side
        table.attach(label, {1, 2, 0, 1}); // insert label into table

        label = ui::Label("y"); // create label
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // align text to the left side
        table.attach(label, {2, 3, 0, 1}); // insert label into table

        label = ui::Label("mapcoordsmins"); // create label
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // align text to the left side
        table.attach(label, {0, 1, 1, 2}); // insert label into table

        auto spinnerMinX = ui::SpinButton(spinner_adj_MinX, 1.0,
                                          0); // create textbox wiht value spin, value and value range
        table.attach(spinnerMinX, {1, 2, 1, 2}); // insert spinbox into table

        auto spinnerMinY = ui::SpinButton(spinner_adj_MinY, 1.0,
                                          0); // create textbox wiht value spin, value and value range
        table.attach(spinnerMinY, {2, 3, 1, 2}); // insert spinbox into table

        label = ui::Label("mapcoordsmaxs"); // create label
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // align text to the left side
        table.attach(label, {0, 1, 2, 3}); // insert label into table

        auto spinnerMaxX = ui::SpinButton(spinner_adj_MaxX, 1.0,
                                          0); // create textbox wiht value spin, value and value range
        table.attach(spinnerMaxX, {1, 2, 2, 3}); // insert spinbox into table

        auto spinnerMaxY = ui::SpinButton(spinner_adj_MaxY, 1.0,
                                          0); // create textbox wiht value spin, value and value range
        table.attach(spinnerMaxY, {2, 3, 2, 3}); // insert spinbox into table

        // put the references to the spinboxes and the worldspawn into the global exchange
        msp.spinner1 = spinnerMinX;
        msp.spinner2 = spinnerMinY;
        msp.spinner3 = spinnerMaxX;
        msp.spinner4 = spinnerMaxY;
        msp.worldspawn = theWorldspawn;

        button = ui::Button("Set"); // create button with text
        button.connect("clicked", G_CALLBACK(set_coordinates), NULL); // connect button with callback function
        table.attach(button, {1, 2, 3, 4}); // insert button into table

        button = ui::Button("Cancel"); // create button with text
        button.connect("clicked", G_CALLBACK(close_window), NULL); // connect button with callback function
        table.attach(button, {2, 3, 3, 4}); // insert button into table
    } else {
        globalOutputStream() << "SunPlug: no worldspawn found!\n"; // output error to console

        auto label = ui::Label(
                "ERROR: No worldspawn was found in the map!\nIn order to use this tool the map must have at least one brush in the worldspawn. "); // create a label
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); // text align left
        vbox.pack_start(label, FALSE, FALSE, 2); // insert the label in the box

        auto button = ui::Button("OK"); // create a button with text
        button.connect("clicked", G_CALLBACK(close_window), NULL); // connect the click event to close the window
        vbox.pack_start(button, FALSE, FALSE, 2); // insert the button in the box
    }

    gtk_window_set_position(window, GTK_WIN_POS_CENTER); // center the window
    gtk_widget_show_all(window); // show the window and all subelements
}
