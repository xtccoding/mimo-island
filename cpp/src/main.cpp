#include <QApplication>
#include "DynamicIsland.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    
    DynamicIsland island;
    island.show();
    
    return app.exec();
}
