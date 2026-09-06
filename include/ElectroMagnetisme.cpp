#include "ElectroMagnetisme.hpp"


void Electrostatique::removeCharge(size_t index) {
    if (index < q.size()) {
        q.erase(q.begin() + index);
    } else {
        throw std::out_of_range("Index out of range.");
    }
}

charge Electrostatique::calcEm(Electrostatique& ElecField , vectorCoord m_point) const
{
    if(ElecField.q.empty()) {
        throw std::runtime_error("No charges in the electric field.");
    }
    vectorCoord EmVec({0.0, 0.0, 0.0});
    charge res(0.0, vectorCoord({0.0, 0.0, 0.0}));
    double norm;
    for(auto &e : ElecField.q)
    {
        vectorCoord relativePosition((m_point - e.getPosition()));
        norm = relativePosition.getNorm();
        if(norm == 0) continue; // Skip if the point coincides with the charge position
        EmVec += (relativePosition*(e.getValue()/(std::pow(norm, 3))));
    }
    EmVec*=COULOMB;
    res.setValue(EmVec.getNorm());
    res.setPosition(EmVec/EmVec.getNorm());
    return res;
}

charge Electrostatique::calcFem(Electrostatique& ElecField, charge testCharge) const
{
    if(ElecField.q.empty()) {
        throw std::runtime_error("No charges in the electric field.");
    }
    vectorCoord EmVec({0.0, 0.0, 0.0});
    vectorCoord relativePosition({0.0, 0.0, 0.0});
    charge res(0.0, vectorCoord({0.0, 0.0, 0.0}));
    double norm;
    for(auto &e : ElecField.q)
    {
        relativePosition = (testCharge.getPosition() - e.getPosition());
        norm = relativePosition.getNorm();
        if(norm == 0) throw std::invalid_argument("Point coincides with a charge position."); // Skip if the point coincides with the charge position
        EmVec += (relativePosition*(e.getValue()/(std::pow(norm, 3))));
    }
    EmVec*=COULOMB*testCharge.getValue();
    res.setValue(EmVec.getNorm());
    res.setPosition(EmVec/EmVec.getNorm());
    return res;
}


charge Electrostatique::calcFemAtQ(Electrostatique& ElecField , unsigned int index) const
{
    if(ElecField.q.empty()) {
        throw std::runtime_error("No charges in the electric field.");
    }
    vectorCoord EmVec({0.0, 0.0, 0.0});
    vectorCoord relativePosition({0.0, 0.0, 0.0});
    charge res(0.0, vectorCoord({0.0, 0.0, 0.0}));
    double norm;
    for(auto &e : ElecField.q)
    {
        relativePosition = (ElecField.q[index].getPosition() - e.getPosition());
        norm = relativePosition.getNorm();
        if(norm == 0) throw std::invalid_argument("Point coincides with a charge position."); // Skip if the point coincides with the charge position
        EmVec += (relativePosition*(e.getValue()/(std::pow(norm, 3))));
    }
    EmVec*=COULOMB*ElecField.q[index].getValue();
    res.setValue(EmVec.getNorm());
    res.setPosition(EmVec/EmVec.getNorm());
    return res;
}



void Electrostatique::printCharges(QTextEdit* Qtxt) const {
    for (const auto& c : q) {
        Qtxt->append(QString("Charge: %1, Position (unitary vector): (%2, %3, %4)")
                        .arg(c.getValue())
                        .arg(c.getPosition().getXcoord())
                        .arg(c.getPosition().getYcoord())
                        .arg(c.getPosition().getZcoord()));

        Qtxt->append(QString("Charge Vector: (%1, %2, %3)")
                        .arg(c.getPosition().getXcoord() * c.getValue())
                        .arg(c.getPosition().getYcoord() * c.getValue())
                        .arg(c.getPosition().getZcoord() * c.getValue()));
    }
}

void Electrostatique::printEm(QTextEdit* Qtxt, Electrostatique& ElecField , vectorCoord m_point) const {
    charge Em = calcEm(ElecField, m_point);
    Qtxt->append(QString("Electric Field Magnitude: %1, Direction (unitary vector): (%2, %3, %4)")
                    .arg(Em.getValue())
                    .arg(Em.getPosition().getXcoord())
                    .arg(Em.getPosition().getYcoord())
                    .arg(Em.getPosition().getZcoord()));

    Qtxt->append(QString("Electric Field Vector: (%1, %2, %3)")
                    .arg(Em.getPosition().getXcoord() * Em.getValue())
                    .arg(Em.getPosition().getYcoord() * Em.getValue())
                    .arg(Em.getPosition().getZcoord() * Em.getValue()));
}

void Electrostatique::printFem(QTextEdit* Qtxt, Electrostatique& ElecField, charge testCharge) const {
    charge Fem = calcFem(ElecField, testCharge);
    Qtxt->append(QString("Electric Force Magnitude: %1, Direction (unitary vector): (%2, %3, %4)")
                    .arg(Fem.getValue())
                    .arg(Fem.getPosition().getXcoord())
                    .arg(Fem.getPosition().getYcoord())
                    .arg(Fem.getPosition().getZcoord()));

    Qtxt->append(QString("Electric Force Vector: (%1, %2, %3)")
                    .arg(Fem.getPosition().getXcoord() * Fem.getValue())
                    .arg(Fem.getPosition().getYcoord() * Fem.getValue())
                    .arg(Fem.getPosition().getZcoord() * Fem.getValue()));
}