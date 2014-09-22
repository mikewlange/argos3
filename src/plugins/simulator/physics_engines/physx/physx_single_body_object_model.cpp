#include "physx_single_body_object_model.h"
#include <argos3/core/simulator/entity/composable_entity.h>

namespace argos {

   /****************************************/
   /****************************************/

   CPhysXSingleBodyObjectModel::CPhysXSingleBodyObjectModel(CPhysXEngine& c_engine,
                                                            CComposableEntity& c_entity) :
      CPhysXModel(c_engine, c_entity.GetComponent<CEmbodiedEntity>("body")),
      m_cEntity(c_entity),
      m_pcGenericBody(NULL) {}

   /****************************************/
   /****************************************/

   CPhysXSingleBodyObjectModel::~CPhysXSingleBodyObjectModel() {
      m_pcGenericBody->release();
   }

   /****************************************/
   /****************************************/

   void CPhysXSingleBodyObjectModel::SetBody(physx::PxRigidActor* pc_body) {
      /* Assign pointer to body */
      m_pcGenericBody = pc_body;
      /* Set userData pointer */
      m_pcGenericBody->userData = this;
      /* Set the flag to say whether the body is static or dynamic */
      m_bIsDynamic = (m_pcGenericBody->is<physx::PxRigidDynamic>() != NULL);
      /* Register the origin anchor update method */
      RegisterAnchorMethod<CPhysXSingleBodyObjectModel>(GetEmbodiedEntity().GetOriginAnchor(),
                                                        &CPhysXSingleBodyObjectModel::UpdateOriginAnchor);
      /* Calculate the bounding box */
      CalculateBoundingBox();
   }

   /****************************************/
   /****************************************/

   void CPhysXSingleBodyObjectModel::Reset() {
      /* No need to reset a static body */
      if(! m_bIsDynamic) return;
      /* Initial position and orientation from embodied entity */
      physx::PxTransform cBodyTrans;
      CVector3ToPxVec3(GetEmbodiedEntity().GetOriginAnchor().Position, cBodyTrans.p);
      CQuaternionToPxQuat(GetEmbodiedEntity().GetOriginAnchor().Orientation, cBodyTrans.q);
      /* Make the body into a kinematic actor to move it */
      m_pcDynamicBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
      /* Move the body to the target position */
      m_pcDynamicBody->setGlobalPose(cBodyTrans);
      /* Reset the body into a dynamic actor */
      m_pcDynamicBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
      /* Clear applied forces and torques */
      m_pcDynamicBody->clearForce();
      m_pcDynamicBody->clearTorque();
   }

   /****************************************/
   /****************************************/

   bool CPhysXSingleBodyObjectModel::MoveTo(const CVector3& c_position,
                                            const CQuaternion& c_orientation,
                                            bool b_check_only) {
      /* Can't move a static body */
      if(! m_bIsDynamic) return false;
      /* Save old body pose */
      physx::PxTransform cOldPose(m_pcDynamicBody->getGlobalPose());
      /* Set target position and orientation */
      physx::PxTransform cBodyTrans;
      CVector3ToPxVec3(c_position, cBodyTrans.p);
      CQuaternionToPxQuat(c_orientation, cBodyTrans.q);
      /* Make the body into a kinematic actor to move it */
      m_pcDynamicBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
      /* Move the body to the target position */
      m_pcDynamicBody->setGlobalPose(cBodyTrans);
      /* Check whether the body is colliding with something */
      bool bIsColliding = IsCollidingWithSomething();
      if(b_check_only || bIsColliding) {
         /* This was only a check or a collision is detected
          * Restore the old pose
          */
         m_pcDynamicBody->setGlobalPose(cOldPose);
      }
      else {
         /* The change is accepted, update the entity */
         UpdateEntityStatus();
      }
      /* Reset the body into a dynamic actor */
      m_pcDynamicBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
      /* Return true if the move was without collisions */
      return !bIsColliding;
   }

   /****************************************/
   /****************************************/

   void CPhysXSingleBodyObjectModel::CalculateBoundingBox() {
      physx::PxBounds3 cPxAABB(m_pcGenericBody->getWorldBounds());
      PxVec3ToCVector3(cPxAABB.minimum, GetBoundingBox().MinCorner);
      PxVec3ToCVector3(cPxAABB.maximum, GetBoundingBox().MaxCorner);
   }

   /****************************************/
   /****************************************/

   void CPhysXSingleBodyObjectModel::UpdateEntityStatus() {
      /* No need to update a static entity */
      if(m_bIsDynamic) {
         CPhysXModel::UpdateEntityStatus();
      }
   }

   /****************************************/
   /****************************************/

   void CPhysXSingleBodyObjectModel::UpdateOriginAnchor(SAnchor& s_anchor) {
      /* Get transform of the ARGoS origin anchor */
      physx::PxTransform cBodyTrans = m_pcGenericBody->getGlobalPose();
      /* Set object position into ARGoS space */
      PxVec3ToCVector3(cBodyTrans.p, s_anchor.Position);
      /* Set object orientation into ARGoS space */
      PxQuatToCQuaternion(cBodyTrans.q, s_anchor.Orientation);
   }

   /****************************************/
   /****************************************/

   bool CPhysXSingleBodyObjectModel::IsCollidingWithSomething() const {
      /* Set query flags to accept any overlap */
      static physx::PxQueryFilterData cQueryFlags(
         physx::PxQueryFlag::eANY_HIT |
         physx::PxQueryFlag::eSTATIC  |
         physx::PxQueryFlag::eDYNAMIC |
         physx::PxQueryFlag::ePREFILTER);
      /* Get body pose transformation */
      physx::PxTransform cTrans(m_pcGenericBody->getGlobalPose());
      /* Buffer to store query results */
      physx::PxOverlapBuffer cOverlapBuf;
      /*
       * Create a filter to exclude some shapes from the overlap results
       */
      CPhysXEngine::CQueryIgnoreShapes cIgnoreShapes;
      /* Exclude ground shape */
      cIgnoreShapes.Ignore(&GetPhysXEngine().GetGroundShape());
      /* Exclude this object's shapes */
      size_t unShapes = m_pcGenericBody->getNbShapes();
      physx::PxShape** ppcShapes = new physx::PxShape*[unShapes];
      m_pcGenericBody->getShapes(ppcShapes, unShapes);
      for(size_t i = 0; i < unShapes; ++i) {
         cIgnoreShapes.Ignore(ppcShapes[i]);
      }
      /*
       * Perform the query
       * It returns true if anything is overlapping this object
       */
      for(size_t i = 0; i < unShapes; ++i) {
         if(GetPhysXEngine().GetScene().overlap(ppcShapes[i]->getGeometry().any(),
                                                cTrans,
                                                cOverlapBuf,
                                                cQueryFlags,
                                                &cIgnoreShapes)) {
            delete[] ppcShapes;
            return true;
         }
      }
      delete[] ppcShapes;
      return false;
   }

   /****************************************/
   /****************************************/

}
