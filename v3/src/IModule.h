#pragma once

// Klipper-style plugin interface.
// Each module registers with Program, which calls run()/stop() on it.
// Modules communicate only through the Program host — never directly.
class IModule {
public:
    virtual const char* name() const = 0;
    virtual void        run()        = 0;
    virtual void        stop()       = 0;
    virtual ~IModule() {}
};
