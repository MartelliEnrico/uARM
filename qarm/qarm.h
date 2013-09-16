/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * uARM
 *
 * Copyright (C) 2013 Marco Melletti
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef QARM_H
#define QARM_H

#include <QMainWindow>
#include <QVBoxLayout>
#include "mainbar.h"
#include "guiConst.h"

class qarm : public QMainWindow{
    Q_OBJECT
public:

    qarm();

private slots:

private:
    QWidget *mainWidget;
    procDisplay *display;
    QVBoxLayout *layout;
    mainBar *toolbar;
};

#endif // QARM_H