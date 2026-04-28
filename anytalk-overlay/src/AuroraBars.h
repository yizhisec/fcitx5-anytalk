#pragma once
#include <QWidget>
#include <QElapsedTimer>

class QPaintEvent;

class AuroraBars : public QWidget {
    Q_OBJECT
public:
    explicit AuroraBars(QWidget *parent = nullptr);

public slots:
    void setLevel(double level);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    double level_ = 0.0;
    QElapsedTimer clock_;
};
