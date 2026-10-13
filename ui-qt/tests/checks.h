#pragma once

#include <QList>
#include <QQmlError>
#include <QStringList>

class Controller;
class QGuiApplication;
class QQmlApplicationEngine;
class UiTheme;

// Development checks. These are compiled only into the kern-checks executable.
int runSelfTest();
// Runs the check selected on the command line. Returns -1 when no check was requested.
int runChecks(QGuiApplication &app, QQmlApplicationEngine &engine, Controller &controller, UiTheme &theme,
              const QStringList &arguments, const QList<QQmlError> &qmlWarnings);
