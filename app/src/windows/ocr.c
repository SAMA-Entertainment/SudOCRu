#include "windows.h"
#include <solver.h>
#include "../utils.h"

struct CellDetails {
    SudOCRu* app;
    unsigned char id;
    GtkButton* button;
};

gboolean SaveCell(GtkButton* button, gpointer user_data)
{
    UNUSED(button);
    struct CellDetails* details = user_data;
    SudOCRu* app = details->app;

    GtkEntry* num_input = GTK_ENTRY(gtk_builder_get_object(app->ui,
                "CellTextBox"));
    GtkStyleContext* ctx = gtk_widget_get_style_context(
            GTK_WIDGET(details->button));
    const gchar *text = gtk_entry_get_text(num_input);
    if (text[0] >= '0' && text[0] <= '9')
    {
        SudokuCell* cell = app->cells[details->id];
        cell->value = text[0] - '0';
        char digit[3] = { 0, };
        if (cell->value != 0) {
            snprintf(digit, sizeof(digit), "%hhu", cell->value);
            gtk_style_context_add_class(ctx, "colored");
        } else {
            gtk_style_context_remove_class(ctx, "colored");
        }
        gtk_button_set_label(details->button, digit);
    } else {
        gtk_style_context_remove_class(ctx, "colored");
    }

    GtkButton* save = GTK_BUTTON(gtk_builder_get_object(app->ui,
                "SaveCellButton"));
    g_signal_handlers_disconnect_by_func(save, G_CALLBACK(SaveCell), details);
    HideWindow(app, "CellModifPopup");
    return TRUE;
}

gboolean CloseEditCell(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    UNUSED(widget);
    UNUSED(event);
    struct CellDetails* details = user_data;
    SudOCRu* app = details->app;
    GtkButton* save = GTK_BUTTON(gtk_builder_get_object(app->ui,
                "SaveCellButton"));
    g_signal_handlers_disconnect_by_func(save, G_CALLBACK(SaveCell), details);
    HideWindow(app, "CellModifPopup");
    return TRUE;
}

gboolean EditCell(GtkButton* button, gpointer user_data)
{
    UNUSED(button);
    struct CellDetails* details = user_data;
    SudOCRu* app = details->app;

    GtkWindow* dialog = GTK_WINDOW(gtk_builder_get_object(app->ui,
                "CellModifPopup"));
    GtkButton* save = GTK_BUTTON(gtk_builder_get_object(app->ui,
                "SaveCellButton"));
    GtkImage* display = GTK_IMAGE(gtk_builder_get_object(app->ui,
                "CellImage"));
    GtkEntry* num_input = GTK_ENTRY(gtk_builder_get_object(app->ui,
                "CellTextBox"));

    SudokuCell* cell = app->cells[details->id];
    DrawImage(cell->data, display);
    char digit[3] = { 0, };
    if (cell->value != 0)
        snprintf(digit, sizeof(digit), "%hhu", cell->value);
    gtk_entry_set_text(num_input, digit);
    gtk_entry_set_alignment(num_input, 0.5);

    char name[14];
    snprintf(name, sizeof(name), "Edit Cell %02hhu", details->id);

    gtk_window_set_title(dialog, name);
    gtk_window_set_destroy_with_parent(dialog, TRUE);
    gtk_window_set_modal(dialog, TRUE);
    g_signal_connect(G_OBJECT(save), "clicked", G_CALLBACK(SaveCell), details);
    g_signal_connect(G_OBJECT(dialog),
        "delete-event", G_CALLBACK(CloseEditCell), details);
    gtk_widget_show(GTK_WIDGET(dialog));
    gtk_window_set_keep_above(dialog, TRUE);
    return TRUE;
}

struct SolveTask {
    SudOCRu* app;
    int result;
};

gboolean DoneSolve(gpointer user_data)
{
    struct SolveTask* task = user_data;
    SudOCRu* app = task->app;
    HideWindow(app, "SolvingPopup");
    if (!task->result)
    {
        HideWindow(app, "OCRCorrection");
        ShowSolveResults(app);
    } else {
        char code[4];
        snprintf(code, sizeof(code), "E%02i", task->result);
        ShowErrorMessage(app, (const char*)&code, "Unable to solve sudoku");
    }
    free(task);
    return G_SOURCE_REMOVE;
}

gpointer ThreadSolveSudoku(gpointer thr_data)
{
    struct SolveTask* task = thr_data;
    SudOCRu* app = task->app;

    PrintStage(1, 2, "Preparing sudoku", 0);
    u8 cells[81];
    for (size_t i = 0; i < sizeof(cells); i++)
    {
        cells[i] = app->cells[i]->value;
    }
    app->sudoku = CreateSudoku((const u8*)&cells, 9, 3);
    int isSolvable = IsSudokuValid(app->sudoku);
    if (isSolvable != 1)
    {
        task->result = isSolvable;
        gdk_threads_add_idle(DoneSolve, task);
        return NULL;
    }
    PrintStage(1, 2, "Preparing sudoku", 1);

    PrintStage(2, 2, "Solving sudoku", 0);
    int solved = Backtracking(app->sudoku, 0);
    if (solved != 1)
    {
        task->result = solved;
        gdk_threads_add_idle(DoneSolve, task);
        return NULL;
    }
    PrintStage(2, 2, "Solving sudoku", 1);
    PrintBoard(app->sudoku);

    task->result = 0;
    gdk_threads_add_idle(DoneSolve, task);
    return NULL;
}

gboolean RunSudokuSolver(GtkButton* button, gpointer user_data)
{
    SudOCRu* app = user_data;
    UNUSED(button);

    struct SolveTask* task = malloc(sizeof(struct SolveTask));
    task->app = app;

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(app->ui,
                 "OCRNextButton")), FALSE);
    ShowLoadingDialog(app, "SolvingPopup");

    PrintProcedure("Sudoku Solver");
    g_thread_new("sudoku_solver", ThreadSolveSudoku, task);
    return TRUE;
}

void SetupOCRResults(SudOCRu* app)
{
    GtkWindow* main = GTK_WINDOW(gtk_builder_get_object(app->ui,
                "MainWindow"));
    GtkWindow* win = GTK_WINDOW(gtk_builder_get_object(app->ui,
                "OCRCorrection"));
    GtkButton* next = GTK_BUTTON(gtk_builder_get_object(app->ui,
                "OCRNextButton"));

    GtkWindowGroup* grp = gtk_window_get_group(main);
    gtk_window_group_add_window(grp, win);
    gtk_window_set_screen(win, gdk_screen_get_default());

    GtkContainer* container = GTK_CONTAINER(gtk_builder_get_object(app->ui,
                "CellGrid"));
    GList *children, *iter;
    GtkStyleContext *ctx;

    children = gtk_container_get_children(container);
    unsigned char i = 80;
    for(iter = children; iter != NULL; iter = g_list_next(iter))
    {
        struct CellDetails* details = malloc(sizeof(struct CellDetails));
        details->app = app;
        details->id = i--;
        details->button = GTK_BUTTON(iter->data);

        ctx = gtk_widget_get_style_context(iter->data);
        gtk_style_context_add_class(ctx, "cell");
        gtk_style_context_remove_class(ctx, "colored");
        gtk_button_set_relief(details->button, GTK_RELIEF_NONE);

        g_signal_connect(GTK_WIDGET(iter->data),
                "clicked", G_CALLBACK(EditCell), details);
    }
    g_list_free(children);

    g_signal_connect(G_OBJECT(win),
        "delete-event", G_CALLBACK(hide_window), NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(hide_window), NULL);
    g_signal_connect(next, "clicked", G_CALLBACK(RunSudokuSolver), app);

    gtk_window_set_destroy_with_parent(win, TRUE);
    gtk_window_set_modal(win, TRUE);
}

void ShowOCRResults(SudOCRu* app)
{
    GtkWindow* win = GTK_WINDOW(gtk_builder_get_object(app->ui,
                "OCRCorrection"));

    GtkImage* img = GTK_IMAGE(gtk_builder_get_object(app->ui,
                "OCRImage"));
    DrawImage(app->cropped_grid, img);

    GtkContainer* container = GTK_CONTAINER(gtk_builder_get_object(app->ui,
                "CellGrid"));
    GtkButton* but;
    GList *children, *iter;
    GtkStyleContext *ctx;

    children = gtk_container_get_children(container);
    unsigned char i = 80;
    char digit[3] = { 0, };
    for(iter = children; iter != NULL; iter = g_list_next(iter))
    {
        ctx = gtk_widget_get_style_context(iter->data);
        but = GTK_BUTTON(iter->data);

        SudokuCell* cell = app->cells[i--];
        if (cell->value != 0) {
            snprintf(digit, sizeof(digit), "%hhu", cell->value);
            gtk_button_set_label(but, digit);
            gtk_style_context_add_class(ctx, "colored");
        } else {
            gtk_button_set_label(but, "");
            gtk_style_context_remove_class(ctx, "colored");
        }
    }
    g_list_free(children);

    gtk_widget_show(GTK_WIDGET(win));
    gtk_window_set_keep_above(win, TRUE);
}
