#ifndef USER_MODEL_H
#define USER_MODEL_H

#include <QString>

class UserModel
{
public:
    UserModel();

    QString getUsername() const;
    void setUsername(const QString &username);

private:
    QString m_username;
};

#endif // USER_MODEL_H
