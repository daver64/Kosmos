#pragma once

#include "model.h"
#include <gtk/gtk.h>

namespace shipedit
{
    class ScenarioPage;
    ScenarioPage *create_scenario_page(GtkWindow *window, const fs::path &root);
    GtkWidget *scenario_page_widget(ScenarioPage *page);
    bool scenario_page_can_close(ScenarioPage *page);
    void destroy_scenario_page(ScenarioPage *page);
}
