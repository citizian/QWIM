#include "user_model.h"

UserModel::UserModel()
{
}

QString UserModel::getUsername() const
{
    return m_username;
}

void UserModel::setUsername(const QString &username)
{
    m_username = username;
}
