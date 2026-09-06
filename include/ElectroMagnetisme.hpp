#ifndef ELECTROMAGNETISME_H
#define ELECTROMAGNETISME_H

#include "mathdt.h"
#include "matrice.hpp"
#include <iostream>

class charge
{
    public:
        charge(double _value, vectorCoord _position) : value(_value), position(_position) {}
        charge(double _value) : value(_value), position(vectorCoord({0.0, 0.0, 0.0})) {}
        ~charge() = default;

        //Setteurs
        void setValue(double _value) { value = _value; }
        void setPosition(vectorCoord _position) { position = _position; }

        //Getteurs
        double getValue() const { return value; }
        vectorCoord getPosition() const { return position; }
        vectorCoord getChargeVector() const { return this->getPosition() * this->getValue(); }
        vectorCoord getChargeVectorUnitary() const { return this->getChargeVector() / this->getChargeVector().getNorm(); }
        double getChargeVectorX() const { return getChargeVector().getXcoord(); }
        double getChargeVectorY() const { return getChargeVector().getYcoord(); }
        double getChargeVectorZ() const { return getChargeVector().getZcoord(); }
        double getChargeVectorNorm() const { return getChargeVector().getNorm(); }

    private:
        double value;
        vectorCoord position;

    protected: 
};

class Electrostatique
{
    public:
        Electrostatique(std::vector<charge> _q) : q(_q) {};
        ~Electrostatique() = default;

        void addCharge(const charge& newCharge) { q.push_back(newCharge); }
        void removeCharge(size_t index);
        charge calcFem(Electrostatique& ElecField, charge testCharge) const;
        charge calcEm(Electrostatique& ElecField , vectorCoord m_point) const;
        charge calcFemAtQ(Electrostatique& ElecField , unsigned int index) const;
        void printCharges(QTextEdit* Qtxt) const;
        void printEm(QTextEdit* Qtxt, Electrostatique& ElecField , vectorCoord m_point) const;
        void printFem(QTextEdit* Qtxt, Electrostatique& ElecField, charge testCharge) const;
    private:
        std::vector<charge> q;

    protected: 
};


#endif /* ELECTROMAGNETISME_H */
