#pragma once

#include <QString>

namespace Credentials {
QString readNgrokToken();
bool writeNgrokToken(const QString &token, QString *error = nullptr);
void clearNgrokToken();
QString readAgentDockToken();
bool writeAgentDockToken(const QString &token, QString *error = nullptr);
}
