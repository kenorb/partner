<?php

require_once 'CRM/Attendance/Form/Search/Custom/Base.php';

class CRM_Attendance_Form_Search_Custom_List
   extends    CRM_Attendance_Form_Search_Custom_Base
   implements CRM_Attendance_Form_Search_Interface {

    protected $_debug = 0;

    function __construct( &$formValues ) {
        parent::__construct( $formValues );
        $this->_columns = array( ts('Contact Id')   => 'contact_id'  ,
                                 ts('Contact Type') => 'contact_type',
                                 ts('Name')         => 'sort_name',
                                 ts('Date Added')   => 'date_added' );
    }

    function buildForm( &$form ) {
        $form->addDate( 'start_date', ts('Start Date'), false, array( 'formatType' => 'custom') );    
        $form->addDate( 'end_date', ts('End Date'), false, array( 'formatType' => 'custom') );
        
        $groups =& CRM_Core_PseudoConstant::group( );
        $inG =& $form->addElement('advmultiselect', 'includeGroups', 
                                  ts('Include Group(s)') . ' ', $groups,
                                  array('size'  => 5,
                                        'style' => 'width:240px',
                                        'class' => 'advmultiselect')
                                  );
        
        $outG =& $form->addElement('advmultiselect', 'excludeGroups', 
                                   ts('Exclude Group(s)') . ' ', $groups,
                                   array('size'  => 5,
                                         'style' => 'width:240px',
                                         'class' => 'advmultiselect')
                                   );
                                   
                               $inG->setButtonAttributes('add',  array('value' => ts('Add >>')));
                               $outG->setButtonAttributes('add', array('value' => ts('Add >>')));
                               $inG->setButtonAttributes('remove',  array('value' => ts('<< Remove')));
                               $outG->setButtonAttributes('remove', array('value' => ts('<< Remove')));
        
        $this->setTitle('Search by date added to CiviCRM');

        //redirect if group not available for search criteria
        if ( count($groups) == 0 ) {
            CRM_Core_Error::statusBounce(  ts("Atleast one Group must be present for search."),
                                           CRM_Utils_System::url( 'civicrm/contact/search/custom/list',
                                                                  'reset=1') );
        }
         /**
         * if you are using the standard template, this array tells the template what elements
         * are part of the search criteria
         */
        $form->assign( 'elements', array( 'start_date', 'end_date', 'includeGroups', 'excludeGroups' ) );
    }

    function summary( ) {
        return null;
    }

    function all( $offset = 0, $rowcount = 0, $sort = null,
                  $includeContactIDs = false ) {
                      
        $this->_includeGroups   = CRM_Utils_Array::value( 'includeGroups', $this->_formValues );

        $this->_excludeGroups   = CRM_Utils_Array::value( 'excludeGroups', $this->_formValues );
        
        $this->_allSearch = false; 
        $this->_groups    = false;
        
        if ( empty( $this->_includeGroups ) && empty( $this->_excludeGroups ) ) {
            //empty search
            $this->_allSearch = true;
        }
        
        if ( ! empty( $this->_includeGroups ) || ! empty( $this->_excludeGroups ) ) {
            //group(s) selected
            $this->_groups = true;
        }
        
        $selectClause = "contact_a.id  as contact_id,
                         contact_a.contact_type as contact_type,
                         contact_a.sort_name    as sort_name,
                         d.date_added           as date_added";

        #$selectClause .= ", GROUP_CONCAT(DISTINCT group_names ORDER BY group_names ASC ) as gname";
        $groupBy = " GROUP BY contact_id ";
        return $this->sql( $selectClause,
                           $offset, $rowcount, $sort,
                           $includeContactIDs, $groupBy );

    }
    
    function from( ) {
        //define table name
        $randomNum = md5( uniqid( ) );
        $this->_tableName = "civicrm_temp_custom_{$randomNum}";

        //grab the contacts added in the date range first
        $sql = "CREATE TEMPORARY TABLE dates_{$this->_tableName} ( id int primary key, date_added date ) ENGINE=HEAP";
        if ($this->_debug > 0) {
            print "-- Date range query: <pre>";
            print "$sql;";
            print "</pre>";
        }
        CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray );
        
        $startDate = CRM_Utils_Date::mysqlToIso( CRM_Utils_Date::processDate( $this->_formValues['start_date'] ) );
        $endDate   = CRM_Utils_Date::mysqlToIso( CRM_Utils_Date::processDate( $this->_formValues['end_date'] ) );
        
        $dateRange =
         "INSERT INTO dates_{$this->_tableName} ( id, date_added )
          SELECT
              civicrm_contact.id,
              min(civicrm_log.modified_date) AS date_added
          FROM
              civicrm_contact LEFT JOIN civicrm_log
              ON (civicrm_contact.id = civicrm_log.entity_id AND
                  civicrm_log.entity_table = 'civicrm_contact')
          GROUP BY
              civicrm_contact.id
          HAVING
              date_added >= '$startDate 00:00:00' AND
              date_added <= '$endDate 23:59:59'"; # tack 11:59pm on to make search inclusive of the end date

        if ($this->_debug > 0) {
            print "-- Date range query: <pre>";
            print "$dateRange;";
            print "</pre>";
        }
        
        CRM_Core_DAO::executeQuery( $dateRange, CRM_Core_DAO::$_nullArray );

        //block for Group search
        $smartGroup = array( );
        require_once 'CRM/Contact/DAO/Group.php';
        $group = new CRM_Contact_DAO_Group( );
        $group->is_active = 1;
        $group->find();
        while( $group->fetch( ) ) {
            $allGroups[] = $group->id;
            if ( $group->saved_search_id ) {
                $smartGroup[$group->saved_search_id] = $group->id;     
            }
        }
        $includedGroups = implode( ',',$allGroups );
        
        if ( ! empty( $this->_includeGroups ) ) { 
            $iGroups = implode( ',', $this->_includeGroups );
        } else {
            //if no group selected search for all groups
            $iGroups = $includedGroups;
        }
        if ( is_array( $this->_excludeGroups ) ) {
            $xGroups = implode( ',', $this->_excludeGroups );
        } else {
            $xGroups = 0;
        }
        
        $sql = "DROP TEMPORARY TABLE IF EXISTS Xg_{$this->_tableName}";
        CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray );           
        $sql = "CREATE TEMPORARY TABLE Xg_{$this->_tableName} ( contact_id int primary key) ENGINE=HEAP";  
        CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray );
        
        //used only when exclude group is selected 
        if( $xGroups != 0 ) {
            $excludeGroup = 
             "INSERT INTO  Xg_{$this->_tableName} ( contact_id )
              SELECT  DISTINCT civicrm_group_contact.contact_id
              FROM civicrm_group_contact, dates_{$this->_tableName} AS d
              WHERE 
                 d.id = civicrm_group_contact.contact_id AND 
                 civicrm_group_contact.status = 'Added' AND
                 civicrm_group_contact.group_id IN( {$xGroups})";
            
            CRM_Core_DAO::executeQuery( $excludeGroup, CRM_Core_DAO::$_nullArray );

            //search for smart group contacts
            foreach( $this->_excludeGroups as $keys => $values ) {
                if ( in_array( $values, $smartGroup ) ) {
                    $ssId = CRM_Utils_Array::key( $values, $smartGroup );
                    
                    $smartSql = CRM_Contact_BAO_SavedSearch::contactIDsSQL( $ssId );
                    
                    $smartSql = $smartSql. " AND contact_a.id NOT IN ( 
                          SELECT contact_id FROM civicrm_group_contact 
                          WHERE civicrm_group_contact.group_id = {$values} AND civicrm_group_contact.status = 'Removed')";
                    
                    $smartGroupQuery = " INSERT IGNORE INTO Xg_{$this->_tableName}(contact_id) $smartSql";
                    
                    CRM_Core_DAO::executeQuery( $smartGroupQuery, CRM_Core_DAO::$_nullArray );
                }
            }
            
        }
        
        $sql = "DROP TEMPORARY TABLE IF EXISTS Ig_{$this->_tableName}";
        CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray );
        $sql = "CREATE TEMPORARY TABLE Ig_{$this->_tableName}
            ( id int PRIMARY KEY AUTO_INCREMENT,
              contact_id int,
              group_names varchar(64)) ENGINE=HEAP";
        
        if ($this->_debug > 0) {
            print "-- Include groups query: <pre>";
            print "$sql;";
            print "</pre>";
        }
        
        CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray );
        
        $includeGroup = 
            "INSERT INTO Ig_{$this->_tableName} (contact_id, group_names)
             SELECT      d.id as contact_id, civicrm_group.name as group_name
             FROM        dates_{$this->_tableName} AS d
             INNER JOIN  civicrm_group_contact
             ON          civicrm_group_contact.contact_id = d.id
             LEFT JOIN   civicrm_group
             ON          civicrm_group_contact.group_id = civicrm_group.id";
         
        //used only when exclude group is selected
        if( $xGroups != 0 ) {
            $includeGroup .= " LEFT JOIN        Xg_{$this->_tableName}
                                      ON        d.id = Xg_{$this->_tableName}.contact_id";
        }
        $includeGroup .= " WHERE           
                                 civicrm_group_contact.status = 'Added'  AND
                                 civicrm_group_contact.group_id IN($iGroups)";

        //used only when exclude group is selected
        if ( $xGroups != 0 ) {
            $includeGroup .=" AND  Xg_{$this->_tableName}.contact_id IS null";
        }
        
        if ($this->_debug > 0) {
            print "-- Include groups query: <pre>";
            print "$includeGroup;";
            print "</pre>";
        }
        
        CRM_Core_DAO::executeQuery( $includeGroup, CRM_Core_DAO::$_nullArray );
        
        //search for smart group contacts
        foreach( $this->_includeGroups as $keys => $values ) {
            if ( in_array( $values, $smartGroup ) ) {
            
                $ssId = CRM_Utils_Array::key( $values, $smartGroup );
        
                $smartSql = CRM_Contact_BAO_SavedSearch::contactIDsSQL( $ssId );
  
                $smartSql .= " AND contact_a.id IN (
                               SELECT id AS contact_id
                               FROM dates_{$this->_tableName} )";
  
                $smartSql .= " AND contact_a.id NOT IN ( 
                               SELECT contact_id FROM civicrm_group_contact
                               WHERE civicrm_group_contact.group_id = {$values} AND civicrm_group_contact.status = 'Removed')";
        
                //used only when exclude group is selected
                if ( $xGroups != 0 ) {
                    $smartSql .= " AND contact_a.id NOT IN (SELECT contact_id FROM  Xg_{$this->_tableName})";
                }
       
                $smartGroupQuery = " INSERT IGNORE INTO
                    Ig_{$this->_tableName}(contact_id) 
                    $smartSql";
        
                CRM_Core_DAO::executeQuery( $smartGroupQuery, CRM_Core_DAO::$_nullArray );
                if ($this->_debug > 0) {
                    print "-- Smart group query: <pre>";
                    print "$smartGroupQuery;";
                    print "</pre>";
                }
                $insertGroupNameQuery = 
                   "UPDATE IGNORE Ig_{$this->_tableName}
                    SET group_names = (SELECT title FROM civicrm_group
                        WHERE civicrm_group.id = $values)
                    WHERE Ig_{$this->_tableName}.contact_id IS NOT NULL 
                        AND Ig_{$this->_tableName}.group_names IS NULL";
                CRM_Core_DAO::executeQuery( $insertGroupNameQuery, CRM_Core_DAO::$_nullArray );
                if ($this->_debug > 0) {
                    print "-- Smart group query: <pre>";
                    print "$insertGroupNameQuery;";
                    print "</pre>";
                }
            }
        }
        
        $from = "FROM civicrm_contact contact_a";
        
        /* We need to join to this again to get the date_added value */
        $from .= " INNER JOIN dates_{$this->_tableName} d ON (contact_a.id = d.id)";
        
        $from .= " INNER JOIN Ig_{$this->_tableName} temptable1 ON (contact_a.id = temptable1.contact_id)";
        
        //this makes smart groups using this search compatible w/ CiviMail
        $from .= " LEFT JOIN civicrm_email ON (contact_a.id = civicrm_email.contact_id)";
        
        return $from;
    }

    function where( $includeContactIDs = false ) {
        return '(1)';
    }

    function templateFile( ) {
        return 'CRM/Attendance/Form/Search/Custom/Sample.tpl';
    }
    
    function setTitle( $title ) {
        if ( $title ) {
            CRM_Utils_System::setTitle( $title );
        } else {
            CRM_Utils_System::setTitle(ts('Search'));
        }
    }
    
    function count( ) {
        $sql = $this->all( );
           
        $dao = CRM_Core_DAO::executeQuery( $sql,
                                             CRM_Core_DAO::$_nullArray );
        return $dao->N;
    }
    
    function __destruct( ) {
        //drop the temp. tables if they exist
        if ( !empty ( $this->_includeGroups ) ) {
            $sql = "DROP TEMPORARY TABLE Ig_{$this->_tableName}";
            CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray ) ;
        }
        
        if ( !empty( $this->_excludeGroups ) ) {
            $sql = "DROP TEMPORARY TABLE Xg_{$this->_tableName}";
            CRM_Core_DAO::executeQuery( $sql, CRM_Core_DAO::$_nullArray ) ;
        }
    }
}


